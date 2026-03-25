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
#include <vector>

// === 你的原始头文件 ===
#include <miniAPI.h>
#include <nise/stub.h>
#include <memscan.h>
#include <inlinehook.h>

#include "nbt/nbt.h"
#include "item/item.h"
#include "item/itemstackbase.h"
#include "item/IFoodItemComponent.h"

// === 全局函数指针 ===
Nbt_treeFind_t Nbt_treeFind = nullptr;
ItemStackBase_getDamageValue_t ItemStackBase_getDamageValue = nullptr;

// ============================================================================
// 日志系统
// ============================================================================
static std::mutex g_log_mutex;
static FILE* g_log_file = nullptr;

static const char* get_time_str() {
    static char buf[64];
    time_t t = time(nullptr);
    tm* tm_info = localtime(&t);
    if (tm_info) strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    else strcpy(buf, "0000-00-00 00:00:00");
    return buf;
}

static void log_init_internal(const char* path) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_file) return;
    g_log_file = fopen(path, "a+");
    if (g_log_file) {
        fprintf(g_log_file, "\n=== Log Started: %s ===\n", get_time_str());
        fflush(g_log_file);
    }
}

static void log_close_internal() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_log_file) return;
    fprintf(g_log_file, "=== Log Closed: %s ===\n", get_time_str());
    fclose(g_log_file);
    g_log_file = nullptr;
}

static void log_write_internal(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_log_file) return;
    fprintf(g_log_file, "[%s] ", get_time_str());
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log_file, fmt, args);
    va_end(args);
    fprintf(g_log_file, "\n");
    fflush(g_log_file);
}

#define log_init(path) log_init_internal(path)
#define log_close() log_close_internal()
#define log_write(...) log_write_internal(__VA_ARGS__)

// ============================================================================
// 辅助函数
// ============================================================================

static std::string buildBarString(int value, const std::string& full, const std::string& half) {
    if (value <= 0) return "";
    std::string out;
    for (int i = 0; i < value; i += 2) out += full;
    if (value % 2 != 0) out += half;
    return out;
}

// 从 ItemStackBase 安全获取物品标识符
static std::string getItemIdentifier(ItemStackBase* stack) {
    if (!stack) return "unknown";
    
    std::string result = "unknown";
    
    // 方法 1: 尝试从 mItem 获取
    try {
        if (stack->mItem.get()) {
            Item* item = stack->mItem.get();
            if constexpr (requires { item->mRawNameId.mStr; }) {
                std::string name = item->mRawNameId.mStr;
                if (!name.empty() && name != "air") {
                    result = name;
                }
            }
        }
    } catch (...) {}
    
    // 方法 2: 如果方法 1 失败，尝试从 NBT 获取
    if (result == "unknown" && stack->mUserData) {
        try {
            if (containsTag(stack->mUserData, "Name")) {
                // NBT Name 标签
            }
        } catch (...) {}
    }
    
    return result;
}

static std::string getItemNamespace(ItemStackBase* stack) {
    if (!stack) return "minecraft";
    
    std::string result = "minecraft";
    
    try {
        if (stack->mItem.get()) {
            Item* item = stack->mItem.get();
            if constexpr (requires { item->mNamespace; }) {
                std::string ns = item->mNamespace;
                if (!ns.empty()) {
                    result = ns;
                }
            }
        }
    } catch (...) {}
    
    return result;
}

static int getItemId(ItemStackBase* stack) {
    if (!stack) return -1;
    
    int result = -1;
    
    try {
        if (stack->mItem.get()) {
            Item* item = stack->mItem.get();
            if constexpr (requires { item->mId; }) {
                result = item->mId;
            }
        }
    } catch (...) {}
    
    return result;
}

// 食物提示
static void addFoodTooltips(IFoodItemComponent* food, std::string& text) {
    if (!food) return;

    int nutrition = 0;
    int saturation = 0;

    try {
        nutrition = food->getNutrition();
        int satMod = food->getSaturationModifier();
        saturation = satMod * nutrition * 2;
    } catch (...) {
        return;
    }

    if (nutrition > 0) {
        text += std::format("\n§eNutrition: {} {}", nutrition, buildBarString(nutrition, "", ""));
    }
    if (saturation > 0) {
        text += std::format("\n§6Saturation: {} {}", saturation, buildBarString(saturation, "", ""));
    }
}

// 蜂箱提示
static void addBeeNestTooltips(ItemStackBase* stack, std::string& text) {
    if (!stack || !stack->mUserData) return;

    void* data = stack->mUserData;
    bool hasTag = containsTag(data, "Occupants") || containsTag(data, "occupants");

    if (!hasTag) {
        text += "\n§7Contains 0 bees§r";
        return;
    }

    auto* list = reinterpret_cast<ListTagLayout*>(getListTag(data, "Occupants"));
    if (!list) list = reinterpret_cast<ListTagLayout*>(getListTag(data, "occupants"));

    if (list) {
        int size = listSize(list);
        text += std::format("\n§7Contains {} bee{}§r", size, size > 1 ? "s" : "");
    }
}

// 耐久度提示
static void addDurabilityTooltips(short maxDamage, ItemStackBase* stack, std::string& text) {
    if (!stack || maxDamage <= 0) return;

    short damage = 0;
    try {
        damage = ItemStackBase_getDamageValue(stack);
    } catch (...) {
        return;
    }

    short current = maxDamage - damage;
    if (current < 0) current = 0;

    std::string color = "§a";
    if (current < maxDamage * 0.25) color = "§c";
    else if (current < maxDamage * 0.5) color = "§e";

    text += std::format("\n{}Durability: {} / {}§r", color, current, maxDamage);
}

// ============================================================================
// Hook 函数
// ============================================================================

static void* g_Item_appendHover_orig = nullptr;
using Item_appendHover_t = void(*)(void*, ItemStackBase*, void*, std::string&, bool);

// Hook 触发计数器（用于调试）
static int g_hook_call_count = 0;

static void Item_appendFormattedHovertext_hook(void* self, ItemStackBase* stack, void* level, std::string& text, bool flag) {
    g_hook_call_count++;
    
    // 1. 调用原始函数
    if (g_Item_appendHover_orig) {
        ((Item_appendHover_t)g_Item_appendHover_orig)(self, stack, level, text, flag);
    } else {
        log_write("[Hook] Original function is NULL!");
        return;
    }

    // 2. 安全检查
    if (!stack) return;

    // 3. 获取 Item 指针
    Item* item = nullptr;
    try {
        if constexpr (requires { stack->mItem; }) {
            item = stack->mItem.get();
        }
    } catch (...) {
        return;
    }

    if (!item) return;

    // 4. 获取物品信息
    std::string rawNameId = getItemIdentifier(stack);
    std::string namespaceStr = getItemNamespace(stack);
    int itemId = getItemId(stack);
    short maxDamage = 0;
    bool isFood = false;

    try {
        maxDamage = item->getMaxDamage();
        isFood = item->isFood();
    } catch (...) {
        return;
    }

    // 5. 每 100 次调用记录一次日志（避免刷屏）
    if (g_hook_call_count % 100 == 1 && !rawNameId.empty() && rawNameId != "air") {
        log_write("[Tooltip #{}] {}:{} | Dmg:{} | Food:{} | ID:{}", 
                  g_hook_call_count, namespaceStr, rawNameId, maxDamage, isFood ? "Y" : "N", itemId);
    }

    // 6. 添加自定义提示

    // 6.1 食物
    if (isFood) {
        IFoodItemComponent* food = nullptr;
        try { food = item->getFood(); } catch (...) {}
        if (food) addFoodTooltips(food, text);
    }

    // 6.2 耐久度
    if (maxDamage > 0) {
        addDurabilityTooltips(maxDamage, stack, text);
    }

    // 6.3 蜂箱
    if (rawNameId == "bee_nest" || rawNameId == "beehive") {
        addBeeNestTooltips(stack, text);
    }

    // 6.4 底部信息（只有当获取到有效信息时才显示）
    if (!rawNameId.empty() && rawNameId != "unknown" && rawNameId != "air") {
        if (itemId > 0) {
            text += std::format("\n§8------------------\n§7{}:{} (#{})§r", namespaceStr, rawNameId, itemId);
        } else {
            text += std::format("\n§8------------------\n§7{}:{}§r", namespaceStr, rawNameId);
        }
    }
}

// ============================================================================
// 签名解析
// ============================================================================

static void* resolve_sig(const char* sig, const char* name) {
    sigscan_handle* handle = sigscan_setup(sig, "libminecraftpe.so", GPWN_SIGSCAN_XMEM);
    if (!handle) {
        log_write("[SigScan] Setup failed for %s", name);
        return nullptr;
    }

    void* func = get_sigscan_result(handle);
    sigscan_cleanup(handle);

    if (func == (void*)-1 || func == nullptr) {
        log_write("[SigScan] Pattern not found for %s", name);
        return nullptr;
    }

    log_write("[SigScan] Found %s at %p", name, func);
    return func;
}

// ============================================================================
// 模块初始化 - 关键修复部分
// ============================================================================

__attribute__((constructor))
static void mod_init() {
    log_init("/sdcard/mc_tooltips.log");
    log_write("=== Module Initializing ===");
    log_write("Game Version Hook Test");

    // 1. 解析符号
    Nbt_treeFind = (Nbt_treeFind_t)resolve_sig(
        "?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 FD 03 00 91 F3 03 00 AA ?? ?? ?? F8 ?? ?? ?? B4 ?? ?? ?? A9 F5 03 13 AA ?? ?? ?? 14 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 91 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? 39 ?? ?? ?? 36 ?? ?? ?? F9 ?? ?? ?? 36 ?? ?? ?? F9 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 34 ?? ?? ?? 37 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 14 ?? ?? ?? 91 ?? ?? ?? 37 ?? ?? ?? D3 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 35 DF 02 18 EB ?? ?? ?? 54 E8 03 1F 2A ?? ?? ?? 71 ?? ?? ?? 54 F5 03 17 AA ?? ?? ?? F9 ?? ?? ?? B5 ?? ?? ?? 14 ?? ?? ?? 54 ?? ?? ?? 17 BF 02 13 EB ?? ?? ?? 54 ?? ?? ?? 39 ?? ?? ?? A9 E0 03 14 AA ?? ?? ?? D3 ?? ?? ?? 72 ?? ?? ?? 91 01 01 8B 9A 57 01 89 9A FF 02 16 EB E2 32 96 9A ?? ?? ?? 94 DF 02 17 EB E8 27 9F 1A 1F 00 00 71 E9 A7 9F 1A 08 01 89 1A 1F 01 00 71 73 12 95 9A E0 03 13 AA ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A8 C0 03 5F D6 ?? ?? ?? A9",
        "Nbt_treeFind"
    );

    ItemStackBase_getDamageValue = (ItemStackBase_getDamageValue_t)resolve_sig(
        "?? ?? ?? D1 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? 91 ?? ?? ?? D5 ?? ?? ?? F9 ?? ?? ?? F8 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4",
        "ItemStackBase_getDamageValue"
    );

    // 2. 注册 Hook - 尝试多个索引和类名组合
    const char* targetLib = "libminecraftpe.so";
    
    // 常见的虚表索引（不同版本不同）
    const int vtableIndices[] = {55, 54, 56, 63, 52, 53, 57, 58};
    
    // 常见的类名格式（不同加载器要求不同）
    const char* classNames[] = {
        "Item",
        "4Item",
        "class Item",
        "minecraft::Item",
        "17ItemRegistry",
        nullptr
    };

    bool hooked = false;
    int usedIndex = -1;
    const char* usedClass = nullptr;

    // 遍历所有组合尝试 Hook
    for (int i = 0; classNames[i] != nullptr && !hooked; i++) {
        for (int j = 0; j < sizeof(vtableIndices)/sizeof(vtableIndices[0]) && !hooked; j++) {
            void* orig = nullptr;
            if (miniAPI::hook::vtable(targetLib, classNames[i], vtableIndices[j], &orig, (void*)Item_appendFormattedHovertext_hook)) {
                g_Item_appendHover_orig = orig;
                hooked = true;
                usedIndex = vtableIndices[j];
                usedClass = classNames[i];
                log_write("[Hook] SUCCESS! Class: %s, Index: %d, Orig: %p", usedClass, usedIndex, orig);
            }
        }
    }

    if (!hooked) {
        log_write("[Hook] FAILED! Could not hook any class/index combination.");
        log_write("[Hook] Try manually finding correct vtable index with IDA.");
    }

    log_write("=== Module Initialized ===");
    log_write("Hook Status: %s", hooked ? "SUCCESS" : "FAILED");
}

__attribute__((destructor))
static void mod_destroy() {
    log_write("=== Module Destroying ===");
    log_write("Total Hook Calls: %d", g_hook_call_count);
    log_close();
}
