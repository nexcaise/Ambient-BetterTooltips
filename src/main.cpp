#include <cstdint>
#include <cstring>
#include <cmath>
#include <format>
#include <string>
#include <miniAPI.h>
#include <Gloss.h>

#include <memscan.h>
#include <inlinehook.h>

#include "nbt/nbt.h"
#include "item/item.h"
#include "item/itemstackbase.h"
#include "item/IFoodItemComponent.h"


#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <ctime>
#include <unistd.h>
#include <sys/mman.h>

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

static int off_mNamespace = -1;
static int off_mRawNameIdStr = -1;
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
    int candidate = nsOffset - 32;
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
    if (ns < 0) return;

    off_mNamespace = ns;
    off_mRawNameIdStr = findRawNameIdStrOffset(item, ns);

    if (off_mRawNameIdStr > 0) {
        int hsStart = (off_mRawNameIdStr - 9) & ~7;
        if (hsStart >= 8)
            off_mId = findIdOffset(item, hsStart);
    }

    offsets_found = true;
}

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

static void hookVtable(void* obj) {
    void** vtable = *(void***)obj;

    if (vtable[55] == (void*)Item_appendFormattedHovertext_hook)
        return;

    if (!g_Item_appendHover_orig)
        g_Item_appendHover_orig = (Item_appendHover_t)vtable[55];

    size_t pageSize = sysconf(_SC_PAGESIZE);
    uintptr_t addr = (uintptr_t)&vtable[55];
    uintptr_t pageStart = addr & ~(pageSize - 1);

    mprotect((void*)pageStart, pageSize, PROT_READ | PROT_WRITE | PROT_EXEC);
    vtable[55] = (void*)Item_appendFormattedHovertext_hook;
    mprotect((void*)pageStart, pageSize, PROT_READ | PROT_EXEC);
}

static void* (*Item_orig)(void*, std::string const&, short) = nullptr;

static void* Item_hook(void* self, std::string const& nameId, short id) {
    void* obj = Item_orig(self, nameId, id);
    if (obj) hookVtable(obj);
    return obj;
}

void HookItem() {
    sigscan_handle *handle = sigscan_setup("FF C3 03 D1 FD 7B 09 A9 FC 6F 0A A9 FA 67 0B A9 F8 5F 0C A9 F6 57 0D A9 F4 4F 0E A9 FD 43 02 91 48 D0 3B D5 49 3C 01 F0", "libminecraftpe.so", GPWN_SIGSCAN_XMEM);
    if(!handle) return;
    
    void *func = get_sigscan_result(handle);
    
    sigscan_cleanup(handle);
    
    if(func == (void*) -1) return;
    
    hook_handle *hookhandle = hook_addr(func, (void*) Item_hook, (void**)&Item_orig, GPWN_AARCH64_MICROHOOK);
    if(!hookhandle) return;
    return;
}

__attribute__((constructor))
static void mod_init() {
    GlossInit(true);

    Nbt_treeFind = (Nbt_treeFind_t)resolve("?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 FD 03 00 91 F3 03 00 AA ?? ?? ?? F8 ?? ?? ?? B4 ?? ?? ?? A9 F5 03 13 AA ?? ?? ?? 14 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 91 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? 39 ?? ?? ?? 36 ?? ?? ?? F9 ?? ?? ?? 36 ?? ?? ?? F9 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 34 ?? ?? ?? 37 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 14 ?? ?? ?? 91 ?? ?? ?? 37 ?? ?? ?? D3 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 35 DF 02 18 EB ?? ?? ?? 54 E8 03 1F 2A ?? ?? ?? 71 ?? ?? ?? 54 F5 03 17 AA ?? ?? ?? F9 ?? ?? ?? B5 ?? ?? ?? 14 ?? ?? ?? 54 ?? ?? ?? 17 BF 02 13 EB ?? ?? ?? 54 ?? ?? ?? 39 ?? ?? ?? A9 E0 03 14 AA ?? ?? ?? D3 ?? ?? ?? 72 ?? ?? ?? 91 01 01 8B 9A 57 01 89 9A FF 02 16 EB E2 32 96 9A ?? ?? ?? 94 DF 02 17 EB E8 27 9F 1A 1F 00 00 71 E9 A7 9F 1A 08 01 89 1A 1F 01 00 71 73 12 95 9A E0 03 13 AA ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A8 C0 03 5F D6 ?? ?? ?? A9","Nbt_treeFind");
        
    ItemStackBase_getDamageValue = (ItemStackBase_getDamageValue_t)resolve("?? ?? ?? D1 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? 91 ?? ?? ?? D5 ?? ?? ?? F9 ?? ?? ?? F8 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4","ItemStackBase_getDamageValue");
    
    HookItem();

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
}
