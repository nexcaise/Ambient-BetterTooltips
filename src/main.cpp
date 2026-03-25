#include <cstdint>
#include <cstring>
#include <cmath>
#include <format>
#include <string>
#include <miniAPI.h>
#include <nise/stub.h>
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

log_init("/sdcard/log.txt");


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
    if(!data || !containsTag(data, "Occupants") && !containsTag(data, "occupants")) {
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

int find_mRawNameId(void* item) {
    for (int i = 0; i < 0x300; i++) {
        void* possible = *(void**)((uintptr_t)item + i);
        if (!possible) continue;

        const char* str = *(const char**)possible;
        if (str && strcmp(str, "diamond_pickaxe") == 0) {
            return i;
        }
    }
    return -1;
}

int find_mNamespace(void* item) {
    for (int i = 0; i < 0x300; i++) {
        std::string* s = (std::string*)((uintptr_t)item + i);
        if (!s) continue;

        const char* c = s->c_str();
        if (c && strcmp(c, "minecraft") == 0) {
            return i;
        }
    }
    return -1;
}

short findIdOffset(void* item) {
    for (int i = 0; i < 0x200; i++) {
        short val = *(short*)((uintptr_t)item + i);
        if (val > 0 && val < 10000) {
            return i;
        }
    }
    return -1;
}

static void Item_appendFormattedHovertext_hook(void* self, ItemStackBase* stack, void* level, std::string& text, bool flag) {
    if (g_Item_appendHover_orig) ((Item_appendHover_t)g_Item_appendHover_orig)(self, stack, level, text, flag);
    
    Item* item = stack->mItem.get();
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

static void* (*Item_orig)(void*, std::string const&, short) = nullptr;

static void* Item_hook(void* self, std::string const& nameId, short id) {
    void* _self = Item_orig(self, nameId, id);

    void** vtable = *(void***)_self;

    if (vtable[55] == (void*)&Item_appendFormattedHovertext_hook)
        return _self;

    if (!g_Item_appendHover_orig)
        g_Item_appendHover_orig = (Item_appendHover_t)vtable[55];

    size_t pageSize = sysconf(_SC_PAGESIZE);
    uintptr_t addr = (uintptr_t)&vtable[55];
    uintptr_t pageStart = addr & ~(pageSize - 1);

    mprotect((void*)pageStart, pageSize, PROT_READ | PROT_WRITE | PROT_EXEC);

    vtable[55] = (void*)&Item_appendFormattedHovertext_hook;

    mprotect((void*)pageStart, pageSize, PROT_READ | PROT_EXEC);

    return _self;
}

void HookItem() {
    sigscan_handle *handle = sigscan_setup("?? ?? ?? D1 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? 91 ?? ?? ?? D5 ?? ?? ?? F9 ?? ?? ?? F8 ?? ?? ?? 39 ?? ?? ?? 34 ?? ?? ?? 12", "libminecraftpe.so", GPWN_SIGSCAN_XMEM);
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
    
    // --- Hook appendFormattedHovertext ---
    // Primary method: miniAPI::hook::vtable (proven working for subclasses).
    // Additionally try to patch _ZTV4Item directly via GlossSymbol + WriteMemory
    // to cover plain Item instances (food items) that miniAPI can't hook.

    // Step 1: Try to manually patch Item's own vtable (for food items / plain Item)
    /*GHandle lib = GlossOpen("libminecraftpe.so");
    if (lib) {
        uintptr_t item_vt = GlossSymbol(lib, "_ZTV4Item", nullptr);
        if (item_vt) {
            void** entries = reinterpret_cast<void**>(item_vt);
            void* orig = entries[2 + 55];
            if (orig) {
                g_Item_appendHover_orig = orig;
                void* hook_func = (void*)Item_appendFormattedHovertext_hook;
                WriteMemory(&entries[2 + 55], &hook_func, sizeof(void*), true);
                log_write("Patched _ZTV4Item slot 55, orig=%p", orig);
            }
        } else {
            log_write("_ZTV4Item not found, food items won't have custom tooltips");
        }
        GlossClose(lib, false);
    }*/

    // Step 2: Hook concrete subclasses via miniAPI (always works).
    // First hook saves orig if step 1 failed; otherwise uses tmp.
//    if (!g_Item_appendHover_orig) {
//        miniAPI::hook::vtable("libminecraftpe.so","9BrushItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
//    } else {
//        void* tmp = nullptr;
//        miniAPI::hook::vtable("libminecraftpe.so","9BrushItem",55,&tmp,(void*)Item_appendFormattedHovertext_hook);
//    }

    HookItem();

    void* tmp = nullptr;
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
