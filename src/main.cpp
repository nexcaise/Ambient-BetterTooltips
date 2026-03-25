#include <cstdint>
#include <cstring>
#include <cmath>
#include <format>
#include <string>
#include <miniAPI.h>
#include <Gloss.h>
#include <nise/stub.h>
#include <memscan.h>

#include "nbt/nbt.h"
#include "item/item.h"
#include "item/itemstackbase.h"
#include "item/IFoodItemComponent.h"


#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <ctime>
#include <unistd.h>

static std::mutex log_mutex;
static FILE* log_file = nullptr;

static const char* get_time_str() {
    static char buf[64];
    time_t t = time(nullptr);
    tm* tm_info = localtime(&t);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    return buf;
}

void log_init(const char* path) {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (log_file) return;
    log_file = fopen(path, "a+");
}

void log_close() {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (!log_file) return;
    fclose(log_file);
    log_file = nullptr;
}

void log_write(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (!log_file) return;

    fprintf(log_file, "[%s] ", get_time_str());

    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);

    fprintf(log_file, "\n");
    fflush(log_file);
}

static void* g_Item_appendHover_orig = nullptr;
using Item_appendHover_t = void(*)(void*, ItemStackBase*, void*, std::string&, bool);

// Generic vtable function caller - calls a virtual function by index
// Returns default value on failure instead of crashing
// Note: vtableIndex must be within valid range (0-150 for typical MCBE classes)
template<typename Ret, typename... Args>
static Ret callVfunc(void* obj, int vtableIndex, Args... args) {
    // Safety checks - limit index to reasonable range
    if (!obj || vtableIndex < 0 || vtableIndex > 150) return Ret{};
    
    void** vtable = *reinterpret_cast<void***>(obj);
    if (!vtable || vtable < (void**)0x10000) return Ret{};  // Basic pointer sanity
    
    void* func = vtable[vtableIndex];
    if (!func || func == (void*)-1 || func < (void*)0x10000) return Ret{};
    
    return reinterpret_cast<Ret(*)(void*, Args...)>(func)(obj, args...);
}

std::string buildBarString(int v, const std::string full, const std::string half) {
    std::string out;
    for (int i = 0; i < (int)v; i+=2) out += full; // 
    if (fmod(v, 2) != 0)
        out += half; // 
    return out;
}

void FoodTooltips(IFoodItemComponent* food, std::string& text) {
    if (!food) return;
    
    int nutrition = 0;
    float saturation = 0.0f;
    
    // Use direct virtual call - safer than vtable scanning
    nutrition = food->getNutrition();
    saturation = food->getSaturationModifier();
    
    // Only display if we got valid values
    if (nutrition > 0 && nutrition <= 20) {
        text += std::format("\n§eNutrition: {}§r", nutrition);
        
        int saturationDisplay = (int)(saturation * nutrition * 2);
        if (saturationDisplay > 0) {
            text += std::format("\n§6Saturation: {}§r", saturationDisplay);
        }
    }
}

void BeeNest(ItemStackBase* stack, std::string& text) {
    void* data = stack->mUserData;
    if(!data || (!containsTag(data, "Occupants") && !containsTag(data, "occupants"))) {
      text += "\n§7Contains 0 bees§r";
      return;
    };
    auto* list = reinterpret_cast<ListTagLayout*>(getListTag(data, "Occupants"));
    if (!list)
        list = reinterpret_cast<ListTagLayout*>(getListTag(data, "occupants"));
    if(list) {
        int size = listSize(list);
        text += std::format("\n§7Contains {} bee{}§r", size, size > 1 ? "s" : "");
    }
}

void ToolDurability(short maxDamage, ItemStackBase* s, std::string& text) {
    short current = maxDamage - ItemStackBase_getDamageValue(s);
    text += std::format("\n§7Durability: {} / {}§r",  current, maxDamage);
}

// Forward declarations for runtime offset discovery
static std::string getItemRawNameId(void* item);

// Attack damage vtable index - discovered at runtime
static int g_AttackDamageVtableIndex = -1;
static bool g_AttackDamageDiscovered = false;

void AttackDamage(void* item, std::string& text) {
    if (!item) return;
    
    std::string rawNameId = getItemRawNameId(item);
    
    // Only check weapons/tools (skip items without damage like food, blocks)
    if (rawNameId.find("sword") == std::string::npos &&
        rawNameId.find("axe") == std::string::npos &&
        rawNameId.find("pickaxe") == std::string::npos &&
        rawNameId.find("shovel") == std::string::npos &&
        rawNameId.find("hoe") == std::string::npos &&
        rawNameId != "trident" &&
        rawNameId != "mace") {
        return;
    }
    
    // Try to discover the correct vtable index for getAttackDamage
    if (!g_AttackDamageDiscovered) {
        // Scan vtable indices 36-60 to find getAttackDamage
        // It should return a reasonable damage value (0.5 - 15.0 for most weapons)
        for (int idx = 36; idx < 60; idx++) {
            float damage = callVfunc<float>(item, idx);
            if (damage > 0.5f && damage < 20.0f) {
                g_AttackDamageVtableIndex = idx;
                log_write("Discovered AttackDamage vtable index: %d, damage=%.1f, item=%s", 
                          idx, damage, rawNameId.c_str());
                break;
            }
        }
        g_AttackDamageDiscovered = true;
    }
    
    // Use discovered index or fallback to common value
    int idx = g_AttackDamageVtableIndex >= 0 ? g_AttackDamageVtableIndex : 37;
    float damage = callVfunc<float>(item, idx);
    
    if (damage > 1.0f && damage < 20.0f) {
        text += std::format("\n§cAttack Damage: {}§r", damage);
    }
}

// Helper to check if item is an ArmorItem by checking item name
static bool isArmorItem(void* item) {
    std::string rawNameId = getItemRawNameId(item);
    return rawNameId.find("helmet") != std::string::npos ||
           rawNameId.find("chestplate") != std::string::npos ||
           rawNameId.find("leggings") != std::string::npos ||
           rawNameId.find("boots") != std::string::npos;
}

// Armor value vtable indices for ArmorItem
// These are after the Item vtable entries
// getArmorValue is typically around index 38-40, getToughnessValue around 39-41
static const int ARMOR_VALUE_VTABLE_INDEX = 38;
static const int TOUGHNESS_VTABLE_INDEX = 39;

void ArmorValue(void* item, std::string& text) {
    if (!item) return;
    
    // Check if this is an armor item by name
    if (!isArmorItem(item)) return;
    
    // Use vtable calls for armor values
    int armor = callVfunc<int>(item, ARMOR_VALUE_VTABLE_INDEX);
    int toughness = callVfunc<int>(item, TOUGHNESS_VTABLE_INDEX);
    
    // Only display if values are reasonable
    if (armor > 0 && armor <= 20) {
        text += std::format("\n§9Armor: {}§r", armor);
    }
    if (toughness > 0 && toughness <= 10) {
        text += std::format("\n§bArmor Toughness: {}§r", toughness);
    }
}

// --- Item field offset discovery ---
// Real MCBE Item class has mRawNameId (HashedString), mNamespace (std::string),
// mId (short) at version-dependent offsets deep inside the class.
// Discover them at runtime by scanning the first vanilla item encountered.
//
// Assumed layout near these fields (typical for MCBE ARM64):
//   ... [mId: short] [padding] [mRawNameId: HashedString(40)] [mNamespace: std::string(24)] ...
// HashedString = { uint64_t hash(8), std::string str(24), HashedString* lastMatch(8) }
static int off_mNamespace = -1;
static int off_mRawNameIdStr = -1;   // offset to raw char data of the item name
static int off_mId = -1;
static bool offsets_found = false;
static int discovery_attempts = 0;

static int findNamespaceOffset(void* item) {
    auto* p = reinterpret_cast<const char*>(item);
    for (int i = 8; i <= 0x300 - 10; i++) {
        if (memcmp(p + i, "minecraft", 10) == 0)
            return i;
    }
    return -1;
}

static int findRawNameIdStrOffset(void* item, int nsOffset) {
    // std::string within HashedString is at HashedString + 8 (after the uint64_t hash)
    // HashedString is 40 bytes total, assumed to be right before mNamespace
    int candidate = nsOffset - 32; // nsOffset - 40 (sizeof HashedString) + 8 (hash)
    if (candidate < 8) return -1;

    auto* s = reinterpret_cast<const char*>(item) + candidate;
    int len = 0;
    for (int j = 0; j < 64 && s[j] != '\0'; j++) {
        char c = s[j];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.'))
            return -1;
        len++;
    }
    return (len >= 2 && len < 64) ? candidate : -1;
}

static int findIdOffset(void* item, int hashedStringOffset) {
    // mId (short) is before the HashedString, but there may be other fields
    // in between (not just zero padding). Search a wider range for a plausible ID.
    for (int delta = 2; delta <= 64; delta += 2) {
        int off = hashedStringOffset - delta;
        if (off < 8) break;
        short val = *reinterpret_cast<short*>((uintptr_t)item + off);
        if (val > 0 && val < 2000) {
            return off;
        }
    }
    return -1;
}

static void tryDiscoverOffsets(void* item) {
    if (offsets_found || discovery_attempts > 100) return;
    discovery_attempts++;

    int ns = findNamespaceOffset(item);
    if (ns < 0) return; // not a vanilla item, will retry on next call

    off_mNamespace = ns;
    off_mRawNameIdStr = findRawNameIdStrOffset(item, ns);

    // Estimate HashedString start from rawNameId char position:
    // In libc++ SSO, chars are at string_object+1, string is at HashedString+8
    // So chars are at HashedString+9, meaning HashedString ~= chars - 9
    // Align down to 8 bytes for struct alignment on ARM64
    if (off_mRawNameIdStr > 0) {
        int hsStart = (off_mRawNameIdStr - 9) & ~7;
        if (hsStart >= 8)
            off_mId = findIdOffset(item, hsStart);
    }

    offsets_found = true;
    log_write("Offsets discovered: mNamespace=0x%X mRawNameIdStr=0x%X mId=0x%X",
              off_mNamespace, off_mRawNameIdStr, off_mId);
}

// Offsets point to raw char data (inside SSO buffers), not std::string objects.
// Read directly as C strings to avoid misinterpreting SSO layout.
static std::string getItemNamespace(void* item) {
    if (off_mNamespace < 0) return "";
    return reinterpret_cast<const char*>((uintptr_t)item + off_mNamespace);
}

static std::string getItemRawNameId(void* item) {
    if (off_mRawNameIdStr < 0) return "";
    return reinterpret_cast<const char*>((uintptr_t)item + off_mRawNameIdStr);
}

static short getItemId(void* item) {
    if (off_mId < 0) return -1;
    return *reinterpret_cast<short*>((uintptr_t)item + off_mId);
}

static void Item_appendFormattedHovertext_hook(void* self, ItemStackBase* stack, void* level, std::string& text, bool flag) {
    if (g_Item_appendHover_orig) ((Item_appendHover_t)g_Item_appendHover_orig)(self, stack, level, text, flag);

    Item* item = stack->mItem.get();
    if (!item) return;

    tryDiscoverOffsets((void*)item);

    short maxDamage = item->getMaxDamage();
    IFoodItemComponent* food = item->getFood();
    std::string rawNameId = getItemRawNameId((void*)item);
    bool isFoodItem = item->isFood();

    // Log for debugging
    log_write("Hook triggered: rawNameId=%s, isFood=%d, food=%p, maxDamage=%d", 
              rawNameId.c_str(), isFoodItem ? 1 : 0, (void*)food, maxDamage);

    // Food tooltips - check if food pointer is valid and try to get nutrition
    if (food != nullptr) {
        int nutrition = food->getNutrition();
        float saturation = food->getSaturationModifier();
        log_write("Food component: nutrition=%d, saturation=%.2f", nutrition, saturation);
        
        if (nutrition > 0 && nutrition <= 20) {
            FoodTooltips(food, text);
        }
    }
    
    // Attack damage for weapons/tools
    AttackDamage((void*)item, text);
    
    // Armor value for armor pieces
    ArmorValue((void*)item, text);
    
    // Tool durability
    if (maxDamage != 0) {
        ToolDurability(maxDamage, stack, text);
    }
    
    // Bee nest/hive
    if (rawNameId == "bee_nest" || rawNameId == "beehive") {
        BeeNest(stack, text);
    }

    // Item ID display
    std::string ns = getItemNamespace((void*)item);
    short id = getItemId((void*)item);
    if (!ns.empty() || !rawNameId.empty()) {
        if (id > 0)
            text += std::format("\n§7{}:{} (#{})§r", ns, rawNameId, id);
        else
            text += std::format("\n§7{}:{}§r", ns, rawNameId);
    }
}

void* resolve(const char *sgig, const char *name) {
    sigscan_handle *handle = sigscan_setup(sgig, "libminecraftpe.so", GPWN_SIGSCAN_XMEM);
    if(!handle) return (void*)0;
    
    void *func = get_sigscan_result(handle);
    
    sigscan_cleanup(handle);
    
    if(func == (void*) -1) return (void*)0;
    return func;
}

__attribute__((constructor))
static void mod_init() {
    GlossInit(true);
    log_init("/sdcard/log.txt");

    Nbt_treeFind = (Nbt_treeFind_t)resolve("?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 FD 03 00 91 F3 03 00 AA ?? ?? ?? F8 ?? ?? ?? B4 ?? ?? ?? A9 F5 03 13 AA ?? ?? ?? 14 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 91 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? 39 ?? ?? ?? 36 ?? ?? ?? F9 ?? ?? ?? 36 ?? ?? ?? F9 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 34 ?? ?? ?? 37 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 14 ?? ?? ?? 91 ?? ?? ?? 37 ?? ?? ?? D3 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 35 DF 02 18 EB ?? ?? ?? 54 E8 03 1F 2A ?? ?? ?? 71 ?? ?? ?? 54 F5 03 17 AA ?? ?? ?? F9 ?? ?? ?? B5 ?? ?? ?? 14 ?? ?? ?? 54 ?? ?? ?? 17 BF 02 13 EB ?? ?? ?? 54 ?? ?? ?? 39 ?? ?? ?? A9 E0 03 14 AA ?? ?? ?? D3 ?? ?? ?? 72 ?? ?? ?? 91 01 01 8B 9A 57 01 89 9A FF 02 16 EB E2 32 96 9A ?? ?? ?? 94 DF 02 17 EB E8 27 9F 1A 1F 00 00 71 E9 A7 9F 1A 08 01 89 1A 1F 01 00 71 73 12 95 9A E0 03 13 AA ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A8 C0 03 5F D6 ?? ?? ?? A9","Nbt_treeFind");
        
    ItemStackBase_getDamageValue = (ItemStackBase_getDamageValue_t)resolve("?? ?? ?? D1 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? 91 ?? ?? ?? D5 ?? ?? ?? F9 ?? ?? ?? F8 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4","ItemStackBase_getDamageValue");
    
    // --- Hook appendFormattedHovertext ---
    // Primary method: miniAPI::hook::vtable (proven working for subclasses).
    // Additionally try to patch _ZTV4Item directly via GlossSymbol + WriteMemory
    // to cover plain Item instances (food items) that miniAPI can't hook.

    // Hook Item base class (for food items and other basic items)
    // This is the main hook that covers all items including food
    miniAPI::hook::vtable("libminecraftpe.so","4Item",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    
    // Hook tool/weapon subclasses (some MCBE versions need these)
    void* tmp = nullptr;
    miniAPI::hook::vtable("libminecraftpe.so","9BrushItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","17FlintAndSteelItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","14FishingRodItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","12CrossbowItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","7BowItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","9BlockItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","18CarrotOnAStickItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","11TridentItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","10ShovelItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","10ShieldItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","10ShearsItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","10DiggerItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","7HoeItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","11PickaxeItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","8MaceItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","11SwordItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","10AxeItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","13ArmorItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
}
