#include <cstdint>
#include <cstring>
#include <cmath>
#include <format>
#include <string>
#include <mutex>
#include <ctime>
#include <cstdio>
#include <cstdarg>
#include <unistd.h>

// --- 保留你原有的所有头文件 ---
#include <miniAPI.h>
#include <nise/stub.h>
#include <memscan.h>
#include <inlinehook.h>

#include "nbt/nbt.h"
#include "item/item.h"
#include "item/itemstackbase.h"
#include "item/IFoodItemComponent.h"

// --- 全局函数指针声明 (必须保留，否则链接失败) ---
// 假设这些是在 nbt.h 或 memscan.h 中声明的，如果是在这里定义，请保留
extern Nbt_treeFind_t Nbt_treeFind;
extern ItemStackBase_getDamageValue_t ItemStackBase_getDamageValue;

// --- 日志系统 ---
static std::mutex log_mutex;
static FILE* log_file = nullptr;

static const char* get_time_str() {
    static char buf[64];
    time_t t = time(nullptr);
    tm* tm_info = localtime(&t);
    if (tm_info) {
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        strcpy(buf, "Unknown Time");
    }
    return buf;
}

void log_init(const char* path) {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (log_file) return;
    log_file = fopen(path, "a+");
    if (log_file) {
        fprintf(log_file, "\n=== Log Started ===\n");
        fflush(log_file);
    }
}

void log_close() {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (!log_file) return;
    fprintf(log_file, "=== Log Closed ===\n");
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

// --- 辅助函数 ---

std::string buildBarString(int v, const std::string& full, const std::string& half) {
    std::string out;
    out.reserve(v / 2 + 2);
    for (int i = 0; i < v; i += 2) {
        out += full;
    }
    if (v % 2 != 0) {
        out += half;
    }
    return out;
}

void FoodTooltips(IFoodItemComponent* food, std::string& text) {
    if (!food) return;
    
    int nutrition = food->getNutrition();
    int saturationModifier = food->getSaturationModifier(); 
    int saturation = saturationModifier * nutrition * 2; 

    if (nutrition > 0) {
        text += std::format("\n{} ({})", buildBarString(nutrition, "", ""), nutrition);
    }
    if (saturation > 0) {
        text += std::format("\n{} ({})", buildBarString(saturation, "", ""), saturation);
    }
}

void BeeNest(ItemStackBase* stack, std::string& text) {
    if (!stack || !stack->mUserData) {
        text += "\n§7Contains 0 bees§r";
        return;
    }

    void* data = stack->mUserData;
    
    // 修复逻辑：如果没有标签，才显示 0
    bool hasOccupants = containsTag(data, "Occupants") || containsTag(data, "occupants");
    
    if (!hasOccupants) {
        text += "\n§7Contains 0 bees§r";
        return;
    }

    auto* list = reinterpret_cast<ListTagLayout*>(getListTag(data, "Occupants"));
    if (!list) {
        list = reinterpret_cast<ListTagLayout*>(getListTag(data, "occupants"));
    }

    if (list) {
        int size = listSize(list);
        text += std::format("\n§7Contains {} bee{}§r", size, size > 1 ? "s" : "");
    } else {
        text += "\n§7Contains ? bees§r";
    }
}

void ToolDurability(short maxDamage, ItemStackBase* s, std::string& text) {
    if (!s) return;
    short current = maxDamage - ItemStackBase_getDamageValue(s);
    if (current < 0) current = 0;
    
    text += std::format("\n§7Durability: {} / {}§r", current, maxDamage);
}

// --- 内存扫描辅助 (已添加基本保护，但仍建议慎用) ---

int find_mRawNameIdOffset(void* item) {
    if (!item) return -1;
    for (int i = 0; i < 0x300; i += sizeof(void*)) { 
        void* possible = *(void**)((uintptr_t)item + i);
        if (!possible) continue;
        const char* str = *(const char**)possible;
        if (str && strlen(str) > 0 && strlen(str) < 50 && strcmp(str, "diamond_pickaxe") == 0) {
            return i;
        }
    }
    return -1;
}

int find_mNamespaceOffset(void* item) {
    if (!item) return -1;
    for (int i = 0; i < 0x300; i += sizeof(std::string)) {
        std::string* s = (std::string*)((uintptr_t)item + i);
        if (!s) continue;
        try {
            const char* c = s->c_str();
            if (c && strlen(c) < 20 && strcmp(c, "minecraft") == 0) {
                return i;
            }
        } catch (...) {
            continue;
        }
    }
    return -1;
}

short findIdOffsetSafe(void* item) {
    if (!item) return -1;
    for (int i = 0; i < 0x200; i += 2) {
        short val = *(short*)((uintptr_t)item + i);
        if (val > 0 && val < 10000) {
            return (short)i;
        }
    }
    return -1;
}

// --- 核心 Hook 函数 ---

static void* g_Item_appendHover_orig = nullptr;
using Item_appendHover_t = void(*)(void*, ItemStackBase*, void*, std::string&, bool);

static void Item_appendFormattedHovertext_hook(void* self, ItemStackBase* stack, void* level, std::string& text, bool flag) {
    // 1. 调用原始函数
    if (g_Item_appendHover_orig) {
        ((Item_appendHover_t)g_Item_appendHover_orig)(self, stack, level, text, flag);
    }

    // 2. 基础安全检查
    if (!stack) {
        log_write("[Error] Hook received null ItemStackBase");
        return;
    }
    
    // 3. 获取 Item 指针
    Item* item = nullptr;
    try {
        // 假设 mItem 是 shared_ptr 或类似智能指针
        if constexpr (requires { stack->mItem; }) {
             item = stack->mItem.get();
        }
    } catch (...) {
        log_write("[Error] Failed to access stack->mItem");
        return;
    }

    if (!item) {
        return;
    }

    // 4. 获取属性
    short maxDamage = 0;
    IFoodItemComponent* food = nullptr;
    std::string rawNameId = "";
    std::string namespaceStr = "";
    int itemId = -1;

    try {
        maxDamage = item->getMaxDamage();
        food = item->getFood();
        
        if constexpr (requires { item->mRawNameId.mStr; }) {
            rawNameId = item->mRawNameId.mStr;
        }
        
        if constexpr (requires { item->mNamespace; }) {
            namespaceStr = item->mNamespace;
        }
        
        if constexpr (requires { item->mId; }) {
            itemId = item->mId;
        }
    } catch (...) {
        log_write("[Error] Exception while reading item properties");
        return;
    }

    // 5. 添加自定义 Tooltip
    
    if (item->isFood() && food != nullptr) {
        FoodTooltips(food, text);
    }

    if (maxDamage != 0) {
        ToolDurability(maxDamage, stack, text);
    }

    if (rawNameId == "bee_nest" || rawNameId == "beehive") {
        BeeNest(stack, text);
    }

    if (!namespaceStr.empty() && !rawNameId.empty()) {
        text += std::format("\n§7{}:{} (#{})§r", namespaceStr, rawNameId, itemId);
    }
    
    // 调试用：如果需要动态查找偏移，取消下面注释 (性能消耗大)
    /*
    int idOff = findIdOffsetSafe((void*)item);
    if(idOff != -1) log_write("Found ID offset: 0x%X", idOff);
    */
}

// --- 签名解析 (使用你的 miniAPI/memscan) ---

void* resolve(const char* sig, const char* name) {
    // 确保 sigscan_setup 等函数在你的 memscan.h 中有定义
    sigscan_handle* handle = sigscan_setup(sig, "libminecraftpe.so", GPWN_SIGSCAN_XMEM);
    if (!handle) {
        log_write("[SigScan] Failed to setup scan for %s", name);
        return nullptr;
    }

    void* func = get_sigscan_result(handle);
    sigscan_cleanup(handle);

    if (func == (void*)-1 || func == nullptr) {
        log_write("[SigScan] Failed to find pattern for %s", name);
        return nullptr;
    }
    
    log_write("[SigScan] Found %s at %p", name, func);
    return func;
}

// --- 模块初始化 ---

__attribute__((constructor))
static void mod_init() {
    // 1. 初始化日志 (修复：移到了函数内部)
    log_init("/sdcard/minecraft_custom_tooltips.log");
    log_write("Module initializing...");

    // 2. 初始化 Gloss (如果不需要可注释)
    // GlossInit(true); 

    // 3. 解析符号 (使用你的 memscan.h 中的函数)
    Nbt_treeFind = (Nbt_treeFind_t)resolve(
        "?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 FD 03 00 91 F3 03 00 AA ?? ?? ?? F8 ?? ?? ?? B4 ?? ?? ?? A9 F5 03 13 AA ?? ?? ?? 14 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 91 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? 39 ?? ?? ?? 36 ?? ?? ?? F9 ?? ?? ?? 36 ?? ?? ?? F9 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 34 ?? ?? ?? 37 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 14 ?? ?? ?? 91 ?? ?? ?? 37 ?? ?? ?? D3 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 35 DF 02 18 EB ?? ?? ?? 54 E8 03 1F 2A ?? ?? ?? 71 ?? ?? ?? 54 F5 03 17 AA ?? ?? ?? F9 ?? ?? ?? B5 ?? ?? ?? 14 ?? ?? ?? 54 ?? ?? ?? 17 BF 02 13 EB ?? ?? ?? 54 ?? ?? ?? 39 ?? ?? ?? A9 E0 03 14 AA ?? ?? ?? D3 ?? ?? ?? 72 ?? ?? ?? 91 01 01 8B 9A 57 01 89 9A FF 02 16 EB E2 32 96 9A ?? ?? ?? 94 DF 02 17 EB E8 27 9F 1A 1F 00 00 71 E9 A7 9F 1A 08 01 89 1A 1F 01 00 71 73 12 95 9A E0 03 13 AA ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A8 C0 03 5F D6 ?? ?? ?? A9",
        "Nbt_treeFind"
    );

    ItemStackBase_getDamageValue = (ItemStackBase_getDamageValue_t)resolve(
        "?? ?? ?? D1 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? 91 ?? ?? ?? D5 ?? ?? ?? F9 ?? ?? ?? F8 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4",
        "ItemStackBase_getDamageValue"
    );

    if (!Nbt_treeFind || !ItemStackBase_getDamageValue) {
        log_write("[Error] Critical symbols missing.");
    }

    // 4. 注册 Hook (使用你的 miniAPI.h 中的函数)
    const char* targetLib = "libminecraftpe.so";
    const int vtableIndex = 55; 

    // 基类
    miniAPI::hook::vtable(targetLib, "4Item", vtableIndex, &g_Item_appendHover_orig, (void*)Item_appendFormattedHovertext_hook);
    
    // 子类 (确保类名字符串与 RTTI 一致)
    const char* classes[] = {
        "9BrushItem", "17FlintAndSteelItem", "14FishingRodItem", "12CrossbowItem",
        "7BowItem", "9BlockItem", "18CarrotOnAStickItem", "11TridentItem",
        "10ShovelItem", "10ShieldItem", "10ShearsItem", "10DiggerItem",
        "7HoeItem", "11PickaxeItem", "8MaceItem"
    };

    for (const char* cls : classes) {
        miniAPI::hook::vtable(targetLib, cls, vtableIndex, &g_Item_appendHover_orig, (void*)Item_appendFormattedHovertext_hook);
    }

    log_write("Module initialized successfully.");
}

__attribute__((destructor))
static void mod_destroy() {
    log_write("Module unloading...");
    log_close();
}
