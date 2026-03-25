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

// NBT 函数前置声明（如果头文件没有提供）
extern "C" {
    bool containsTag(void* data, const char* tag);
    void* getListTag(void* data, const char* tag);
    int listSize(void* list);
}

// 函数指针类型定义
using Item_appendHover_t = void(*)(void*, ItemStackBase*, void*, std::string&, bool);
using ItemStackBase_getDamageValue_t = short(*)(ItemStackBase*);
using Nbt_treeFind_t = void*(*)(void*, const char*);

// 全局函数指针
Nbt_treeFind_t Nbt_treeFind = nullptr;
ItemStackBase_getDamageValue_t ItemStackBase_getDamageValue = nullptr;

// 日志系统
static std::mutex log_mutex;
static FILE* log_file = nullptr;

static const char* get_time_str() {
    static thread_local char buf[64];
    time_t t = time(nullptr);
    tm* tm_info = localtime(&t);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    return buf;
}

void log_init(const char* path) {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (log_file) return;
    log_file = fopen(path, "a+");
    if(log_file) {
        fprintf(log_file, "\n=== Log started at %s ===\n", get_time_str());
        fflush(log_file);
    }
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

// 每个类一个独立的 orig 指针
static void* g_Item_appendHover_orig_Item = nullptr;
static void* g_Item_appendHover_orig_BrushItem = nullptr;
static void* g_Item_appendHover_orig_FlintAndSteelItem = nullptr;
static void* g_Item_appendHover_orig_FishingRodItem = nullptr;
static void* g_Item_appendHover_orig_CrossbowItem = nullptr;
static void* g_Item_appendHover_orig_BowItem = nullptr;
static void* g_Item_appendHover_orig_BlockItem = nullptr;
static void* g_Item_appendHover_orig_CarrotOnAStickItem = nullptr;
static void* g_Item_appendHover_orig_TridentItem = nullptr;
static void* g_Item_appendHover_orig_ShovelItem = nullptr;
static void* g_Item_appendHover_orig_ShieldItem = nullptr;
static void* g_Item_appendHover_orig_ShearsItem = nullptr;
static void* g_Item_appendHover_orig_DiggerItem = nullptr;
static void* g_Item_appendHover_orig_HoeItem = nullptr;
static void* g_Item_appendHover_orig_PickaxeItem = nullptr;
static void* g_Item_appendHover_orig_MaceItem = nullptr;

// 工具函数
std::string buildBarString(int v, const std::string full, const std::string half) {
    std::string out;
    for (int i = 0; i < v; i += 2) out += full;
    if (v % 2 != 0) out += half;
    return out;
}

void FoodTooltips(IFoodItemComponent* food, std::string& text) {
    if (!food) return;
    int nutrition = food->getNutrition();
    float satMod = food->getSaturationModifier();
    int saturation = (int)(satMod * nutrition * 2.0f);
    
    log_write("FoodTooltips: nutrition=%d, satMod=%f, saturation=%d", nutrition, satMod, saturation);
    
    if(nutrition > 0) 
        text += std::format("\n{} ({})", buildBarString(nutrition, "", ""), nutrition);
    if(saturation > 0) 
        text += std::format("\n{} ({})", buildBarString(saturation, "", ""), saturation);
}

void BeeNest(ItemStackBase* stack, std::string& text) {
    void* data = stack->mUserData;
    log_write("BeeNest: mUserData=%p", data);
    
    if(!data) {
        text += "\n§7Contains 0 bees§r";
        return;
    }
    
    bool hasOccupants = containsTag(data, "Occupants") || containsTag(data, "occupants");
    log_write("BeeNest: hasOccupants=%d", hasOccupants);
    
    if(!hasOccupants) {
        text += "\n§7Contains 0 bees§r";
        return;
    }
    
    void* list = getListTag(data, "Occupants");
    if (!list) list = getListTag(data, "occupants");
    
    if(list) {
        int size = listSize(list);
        log_write("BeeNest: listSize=%d", size);
        text += std::format("\n§7Contains {} bee{}§r", size, size > 1 ? "s" : "");
    } else {
        text += "\n§7Contains ? bees§r";
    }
}

void ToolDurability(short maxDamage, ItemStackBase* s, std::string& text) {
    if (!ItemStackBase_getDamageValue) {
        log_write("ToolDurability: ItemStackBase_getDamageValue is null!");
        return;
    }
    short damage = ItemStackBase_getDamageValue(s);
    short current = maxDamage - damage;
    log_write("ToolDurability: max=%d, damage=%d, current=%d", maxDamage, damage, current);
    text += std::format("\n§7Durability: {} / {}§r", current, maxDamage);
}

// 主 Hook 函数
static void Item_appendFormattedHovertext_hook(void* self, ItemStackBase* stack, void* level, std::string& text, bool flag) {
    // 调用原始函数（通过全局指针，实际应该通过参数传入，但 miniAPI 可能不支持）
    // 注意：这里需要知道调用的是哪个类的 orig，但 miniAPI 的 vtable hook 通常会自动处理
    
    log_write("=== Hook called ===");
    log_write("self=%p, stack=%p, level=%p, flag=%d", self, stack, level, flag);
    
    if (!stack) {
        log_write("ERROR: stack is null");
        return;
    }
    
    Item* item = stack->mItem.get();
    log_write("item=%p", item);
    
    if (!item) {
        log_write("ERROR: item is null");
        return;
    }
    
    // 安全获取属性
    short maxDamage = 0;
    try {
        maxDamage = item->getMaxDamage();
        log_write("maxDamage=%d", maxDamage);
    } catch(...) {
        log_write("Exception in getMaxDamage");
    }
    
    // 获取食物组件
    IFoodItemComponent* food = nullptr;
    try {
        if(item->isFood()) {
            food = item->getFood();
            log_write("isFood=true, food=%p", food);
        }
    } catch(...) {
        log_write("Exception in getFood/isFood");
    }
    
    // 获取名称（安全方式）
    std::string rawNameId;
    try {
        rawNameId = item->mRawNameId.mStr;
        log_write("rawNameId=%s", rawNameId.c_str());
    } catch(...) {
        log_write("Exception reading mRawNameId");
        rawNameId = "unknown";
    }
    
    // 添加提示信息
    try {
        if(food) {
            log_write("Adding food tooltips...");
            FoodTooltips(food, text);
        }
        
        if(maxDamage > 0) {
            log_write("Adding durability...");
            ToolDurability(maxDamage, stack, text);
        }
        
        if(rawNameId == "bee_nest" || rawNameId == "beehive") {
            log_write("Adding bee nest info...");
            BeeNest(stack, text);
        }
        
        // 添加 ID 信息
        std::string ns = "unknown";
        short id = 0;
        try {
            ns = item->mNamespace;
            id = item->mId;
        } catch(...) {}
        
        text += std::format("\n§7{}:{} (#{})§r", ns, rawNameId, id);
        log_write("Final text length: %zu", text.length());
        
    } catch(const std::exception& e) {
        log_write("Exception in tooltip generation: %s", e.what());
    } catch(...) {
        log_write("Unknown exception in tooltip generation");
    }
    
    log_write("=== Hook end ===");
}

// 简化的 hook 安装（每个类独立）
#define HOOK_VTABLE(className, varName) \
    miniAPI::hook::vtable("libminecraftpe.so", className, 55, &varName, (void*)Item_appendFormattedHovertext_hook)

void* resolve(const char *sig, const char *name) {
    log_write("Resolving: %s", name);
    sigscan_handle *handle = sigscan_setup(sig, "libminecraftpe.so", GPWN_SIGSCAN_XMEM);
    if(!handle) {
        log_write("Failed to setup sigscan for %s", name);
        return nullptr;
    }
    
    void *func = get_sigscan_result(handle);
    sigscan_cleanup(handle);
    
    if(func == (void*)-1 || !func) {
        log_write("Failed to resolve %s", name);
        return nullptr;
    }
    
    log_write("Resolved %s at %p", name, func);
    return func;
}

__attribute__((constructor))
static void mod_init() {
    log_init("/sdcard/itemtooltips.log");  // 改名避免冲突
    
    log_write("=== Mod Init Start ===");
    
    GlossInit(true);

    // 解析 NBT 函数
    Nbt_treeFind = (Nbt_treeFind_t)resolve("?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 FD 03 00 91 F3 03 00 AA ?? ?? ?? F8 ?? ?? ?? B4 ?? ?? ?? A9 F5 03 13 AA ?? ?? ?? 14 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 91 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? 39 ?? ?? ?? 36 ?? ?? ?? F9 ?? ?? ?? 36 ?? ?? ?? F9 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 34 ?? ?? ?? 37 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 14 ?? ?? ?? 91 ?? ?? ?? 37 ?? ?? ?? D3 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 35 DF 02 18 EB ?? ?? ?? 54 E8 03 1F 2A ?? ?? ?? 71 ?? ?? ?? 54 F5 03 17 AA ?? ?? ?? F9 ?? ?? ?? B5 ?? ?? ?? 14 ?? ?? ?? 54 ?? ?? ?? 17 BF 02 13 EB ?? ?? ?? 54 ?? ?? ?? 39 ?? ?? ?? A9 E0 03 14 AA ?? ?? ?? D3 ?? ?? ?? 72 ?? ?? ?? 91 01 01 8B 9A 57 01 89 9A FF 02 16 EB E2 32 96 9A ?? ?? ?? 94 DF 02 17 EB E8 27 9F 1A 1F 00 00 71 E9 A7 9F 1A 08 01 89 1A 1F 01 00 71 73 12 95 9A E0 03 13 AA ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A8 C0 03 5F D6 ?? ?? ?? A9", "Nbt_treeFind");
    
    ItemStackBase_getDamageValue = (ItemStackBase_getDamageValue_t)resolve("?? ?? ?? D1 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? 91 ?? ?? ?? D5 ?? ?? ?? F9 ?? ?? ?? F8 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4", "ItemStackBase_getDamageValue");
    
    // 安装 Hooks - 注意：miniAPI 的 vtable hook 可能需要特殊处理
    // 如果 miniAPI 支持链式调用（自动调用原函数），则不需要单独存储 orig
    // 否则需要为每个类单独处理
    
    log_write("Installing hooks...");
    
    // 基础 Item 类
    HOOK_VTABLE("4Item", g_Item_appendHover_orig_Item);
    
    // 子类 - 注意：如果子类没有重写这个方法，会继承父类的 vtable 条目
    // 这种情况下 hook 父类就足够了
    // 但如果子类重写了，就需要单独 hook
    
    // 工具类（通常继承 DiggerItem）
    HOOK_VTABLE("10DiggerItem", g_Item_appendHover_orig_DiggerItem);
    HOOK_VTABLE("10ShovelItem", g_Item_appendHover_orig_ShovelItem);
    HOOK_VTABLE("11PickaxeItem", g_Item_appendHover_orig_PickaxeItem);
    HOOK_VTABLE("7HoeItem", g_Item_appendHover_orig_HoeItem);
    
    // 其他工具
    HOOK_VTABLE("9BrushItem", g_Item_appendHover_orig_BrushItem);
    HOOK_VTABLE("17FlintAndSteelItem", g_Item_appendHover_orig_FlintAndSteelItem);
    HOOK_VTABLE("14FishingRodItem", g_Item_appendHover_orig_FishingRodItem);
    HOOK_VTABLE("12CrossbowItem", g_Item_appendHover_orig_CrossbowItem);
    HOOK_VTABLE("7BowItem", g_Item_appendHover_orig_BowItem);
    HOOK_VTABLE("9BlockItem", g_Item_appendHover_orig_BlockItem);
    HOOK_VTABLE("18CarrotOnAStickItem", g_Item_appendHover_orig_CarrotOnAStickItem);
    HOOK_VTABLE("11TridentItem", g_Item_appendHover_orig_TridentItem);
    HOOK_VTABLE("10ShieldItem", g_Item_appendHover_orig_ShieldItem);
    HOOK_VTABLE("10ShearsItem", g_Item_appendHover_orig_ShearsItem);
    HOOK_VTABLE("8MaceItem", g_Item_appendHover_orig_MaceItem);
    
    log_write("=== Mod Init End ===");
}
