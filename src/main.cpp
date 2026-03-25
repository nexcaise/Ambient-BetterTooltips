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

std::string buildBarString(int v, const std::string full, const std::string half) {
    std::string out;
    for (int i = 0; i < (int)v; i+=2) out += full; // 
    if (fmod(v, 2) != 0)
        out += half; // 
    return out;
}

void FoodTooltips(IFoodItemComponent* food, std::string& text) {
    int nutrition = food->getNutrition();
    int saturation = food->getSaturationModifier() * nutrition * 2;
    if(nutrition > 0) text += std::format("\n{} ({})", buildBarString(nutrition, "", ""), nutrition);
    if(saturation > 0) text += std::format("\n{} ({})", buildBarString(saturation, "", ""), saturation);
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
    
    if(item->isFood() && food != nullptr) FoodTooltips(food, text); 
  	if(maxDamage != 0) ToolDurability(maxDamage, stack, text);
  	if(rawNameId == "bee_nest" || rawNameId == "beehive") BeeNest(stack, text);

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
    
    // Hook appendFormattedHovertext by directly patching vtable entries.
    // Unlike inline hooking, this keeps the original function body intact —
    // g_Item_appendHover_orig is the real function address (no trampoline),
    // so calling it reliably preserves vanilla behavior (attack damage, etc.).
    //
    // Strategy: read vtable slot 55 from many subclasses, find the most
    // common function address (base Item implementation), then patch ALL
    // vtable entries that point to it.
    GHandle lib = GlossOpen("libminecraftpe.so");
    if (lib) {
        static const char* vtable_syms[] = {
            "_ZTV4Item",
            "_ZTV9BrushItem", "_ZTV9BlockItem", "_ZTV10DiggerItem",
            "_ZTV7BowItem", "_ZTV7HoeItem", "_ZTV10ShovelItem",
            "_ZTV10ShearsItem", "_ZTV10ShieldItem", "_ZTV11TridentItem",
            "_ZTV11PickaxeItem", "_ZTV8MaceItem", "_ZTV12CrossbowItem",
            "_ZTV17FlintAndSteelItem", "_ZTV14FishingRodItem",
            "_ZTV18CarrotOnAStickItem",
            nullptr
        };

        uintptr_t vtables[16] = {};
        void* addrs[16] = {};
        int count = 0;

        for (int i = 0; vtable_syms[i] && count < 16; i++) {
            uintptr_t vt = GlossSymbol(lib, vtable_syms[i], nullptr);
            if (vt) {
                void** entries = reinterpret_cast<void**>(vt);
                void* func = entries[2 + 55];
                if (func) {
                    log_write("vtable %s slot 55 -> %p", vtable_syms[i], func);
                    vtables[count] = vt;
                    addrs[count] = func;
                    count++;
                }
            }
        }

        // Find the most common address (base Item::appendFormattedHovertext)
        void* base_func = nullptr;
        int best_freq = 0;
        for (int i = 0; i < count; i++) {
            int freq = 0;
            for (int j = 0; j < count; j++) {
                if (addrs[j] == addrs[i]) freq++;
            }
            if (freq > best_freq) {
                best_freq = freq;
                base_func = addrs[i];
            }
        }

        if (base_func) {
            // Save the real original function pointer (direct address, no trampoline)
            g_Item_appendHover_orig = base_func;
            void* hook_func = (void*)Item_appendFormattedHovertext_hook;

            // Patch every vtable that uses the base function
            int patched = 0;
            for (int i = 0; i < count; i++) {
                if (addrs[i] == base_func) {
                    void** entries = reinterpret_cast<void**>(vtables[i]);
                    WriteMemory(&entries[2 + 55], &hook_func, sizeof(void*), true);
                    patched++;
                }
            }
            log_write("Patched %d/%d vtables, base func=%p", patched, count, base_func);
        } else {
            log_write("Failed to find appendFormattedHovertext");
        }

        GlossClose(lib, false);
    }
}
