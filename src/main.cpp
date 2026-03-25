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

// ========== 修复1：删除了全局作用域的log_init调用（C++不允许全局直接调用函数，这是第一个编译错误的根源） ==========


static void* g_Item_appendHover_orig = nullptr;
using Item_appendHover_t = void(*)(void*, ItemStackBase*, void*, std::string&, bool);

// ========== 修复2：新增偏移量缓存，避免每次hook都遍历内存（同时解决原函数返回int却调用.c_str()的核心错误） ==========
static int mRawNameId_offset = -1;
static int mNamespace_offset = -1;
static short mId_offset = -1;

std::string buildBarString(int v, const std::string full, const std::string half) {
    std::string out;
    for (int i = 0; i < (int)v; i+=2) out += full;
    if (fmod(v, 2) != 0)
        out += half;
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

// ========== 修复3：修正find函数逻辑，只用来找偏移量，返回int类型的偏移值 ==========
int find_mRawNameId(void* item) {
    for (int i = 0; i < 0x300; i += sizeof(void*)) { // 按指针步长遍历，避免无效访问
        uintptr_t addr = (uintptr_t)item + i;
        if (!addr) continue;
        
        void* str_ptr = *(void**)addr;
        if (!str_ptr) continue;

        const char* str = *(const char**)str_ptr;
        if (str && strcmp(str, "diamond_pickaxe") == 0) {
            return i;
        }
    }
    return -1;
}

int find_mNamespace(void* item) {
    for (int i = 0; i < 0x300; i += sizeof(std::string)) { // 按string对象步长遍历
        uintptr_t addr = (uintptr_t)item + i;
        if (!addr) continue;

        // 安全校验：先判断是否是有效的string对象，避免崩溃
        try {
            std::string* s = (std::string*)addr;
            const char* c = s->c_str();
            if (c && strcmp(c, "minecraft") == 0) {
                return i;
            }
        } catch (...) {
            continue;
        }
    }
    return -1;
}

short findIdOffset(void* item) {
    for (int i = 0; i < 0x200; i += sizeof(short)) { // 按short步长遍历，避免无效访问
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
    if (!item) return; // 新增空指针校验，避免崩溃
    
    short maxDamage = item->getMaxDamage();
    IFoodItemComponent* food = item->getFood();
    std::string rawNameId = item->mRawNameId.mStr;
    
    // ========== 修复4：修正原代码的核心错误，先找偏移量，再通过偏移量拿字符串 ==========
    log_write("Hello world");
    
    // 只在第一次调用时找偏移量，缓存下来，避免每次都遍历内存
    if (mId_offset == -1) mId_offset = findIdOffset((void*)item);
    if (mRawNameId_offset == -1) mRawNameId_offset = find_mRawNameId((void*)item);
    if (mNamespace_offset == -1) mNamespace_offset = find_mNamespace((void*)item);
    
    // 打印偏移量
    log_write("mId offset: 0x%X", mId_offset);
    log_write("mRawNameId offset: 0x%X", mRawNameId_offset);
    log_write("mNamespace offset: 0x%X", mNamespace_offset);
    
    // 安全获取并打印字符串（解决原int调用.c_str()的编译错误）
    if (mRawNameId_offset != -1) {
        void* str_ptr = *(void**)((uintptr_t)item + mRawNameId_offset);
        if (str_ptr) {
            const char* rawName = *(const char**)str_ptr;
            log_write("Namespace full: %s", rawName ? rawName : "null");
        }
    }
    if (mNamespace_offset != -1) {
        std::string* namespace_str = (std::string*)((uintptr_t)item + mNamespace_offset);
        log_write("Namespace: %s", namespace_str->c_str());
    }
    
    if(item->isFood() && food != nullptr) FoodTooltips(food, text); 
  	if(maxDamage != 0) ToolDurability(maxDamage,stack,text);
  	if(rawNameId == "bee_nest" || rawNameId == "beehive") BeeNest(stack, text);
  
  	text += std::format("\n§7{}:{} (#{})§r", item->mNamespace, rawNameId, item->mId);
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
    // ========== 修复5：把log_init移到这里，模块加载时自动初始化日志 ==========
    log_init("/sdcard/log.txt");
    
    GlossInit(true);

    Nbt_treeFind = (Nbt_treeFind_t)resolve("?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 FD 03 00 91 F3 03 00 AA ?? ?? ?? F8 ?? ?? ?? B4 ?? ?? ?? A9 F5 03 13 AA ?? ?? ?? 14 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 91 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? 39 ?? ?? ?? 36 ?? ?? ?? F9 ?? ?? ?? 36 ?? ?? ?? F9 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 34 ?? ?? ?? 37 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 14 ?? ?? ?? 91 ?? ?? ?? 37 ?? ?? ?? D3 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 35 DF 02 18 EB ?? ?? ?? 54 E8 03 1F 2A ?? ?? ?? 71 ?? ?? ?? 54 F5 03 17 AA ?? ?? ?? F9 ?? ?? ?? B5 ?? ?? ?? 14 ?? ?? ?? 54 ?? ?? ?? 17 BF 02 13 EB ?? ?? ?? 54 ?? ?? ?? 39 ?? ?? ?? A9 E0 03 14 AA ?? ?? ?? D3 ?? ?? ?? 72 ?? ?? ?? 91 01 01 8B 9A 57 01 89 9A FF 02 16 EB E2 32 96 9A ?? ?? ?? 94 DF 02 17 EB E8 27 9F 1A 1F 00 00 71 E9 A7 9F 1A 08 01 89 1A 1F 01 00 71 73 12 95 9A E0 03 13 AA ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A8 C0 03 5F D6 ?? ?? ?? A9","Nbt_treeFind");
        
    ItemStackBase_getDamageValue = (ItemStackBase_getDamageValue_t)resolve("?? ?? ?? D1 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? 91 ?? ?? ?? D5 ?? ?? ?? F9 ?? ?? ?? F8 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4","ItemStackBase_getDamageValue");
    
    miniAPI::hook::vtable("libminecraftpe.so","4Item",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    
    miniAPI::hook::vtable("libminecraftpe.so","9BrushItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","17FlintAndSteelItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","14FishingRodItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","12CrossbowItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","7BowItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","9BlockItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","18CarrotOnAStickItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","11TridentItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","10ShovelItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","10ShieldItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","10ShearsItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","10DiggerItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","7HoeItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","11PickaxeItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","8MaceItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
}

// ========== 新增：模块卸载时关闭日志，避免文件句柄泄漏 ==========
__attribute__((destructor))
static void mod_unload() {
    log_close();
}
