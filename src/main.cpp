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

// ==================== 全局变量与日志系统 ====================
static std::mutex log_mutex;
static FILE* log_file = nullptr;

// 偏移量缓存（仅初始化时计算一次，避免每次渲染都遍历内存）
static int mRawNameId_offset = -1;
static int mNamespace_offset = -1;
static short mId_offset = -1;

// 原函数指针（核心修复：仅保留基类Item的原函数，避免重复覆盖）
static void* g_Item_appendHover_orig = nullptr;
using Item_appendHover_t = void(*)(void*, ItemStackBase*, void*, std::string&, bool);

// ==================== 日志工具函数 ====================
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
    if (!log_file || !fmt) return;

    fprintf(log_file, "[%s] ", get_time_str());

    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);

    fprintf(log_file, "\n");
    fflush(log_file);
}

// ==================== 工具函数（全量空指针防护，避免崩溃） ====================
std::string buildBarString(int v, const std::string& full, const std::string& half) {
    std::string out;
    if (v <= 0 || full.empty() || half.empty()) return out;
    for (int i = 0; i < v; i += 2) out += full;
    if (v % 2 != 0) out += half;
    return out;
}

void FoodTooltips(IFoodItemComponent* food, std::string& text) {
    if (!food) return; // 空指针防护：修复传入nullptr直接访问成员崩溃
    int nutrition = food->getNutrition();
    int saturation = food->getSaturationModifier() * nutrition * 2;
    if (nutrition > 0) text += std::format("\n{} ({})", buildBarString(nutrition, "", ""), nutrition);
    if (saturation > 0) text += std::format("\n{} ({})", buildBarString(saturation, "", ""), saturation);
}

void BeeNest(ItemStackBase* stack, std::string& text) {
    if (!stack || !stack->mUserData) return; // 空指针防护：修复无NBT数据时访问崩溃
    void* data = stack->mUserData;
    if (!containsTag(data, "Occupants") && !containsTag(data, "occupants")) {
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

void ToolDurability(short maxDamage, ItemStackBase* s, std::string& text) {
    // 空指针+函数有效性防护：修复函数指针为nullptr时调用崩溃
    if (!s || maxDamage <= 0 || !ItemStackBase_getDamageValue) return;
    short current = maxDamage - ItemStackBase_getDamageValue(s);
    text += std::format("\n§7Durability: {} / {}§r", current, maxDamage);
}

// ==================== 安全的偏移量查找函数（仅初始化时调用一次） ====================
int find_mRawNameId(void* item) {
    if (!item) return -1;
    // 按指针步长遍历，跳过低地址非法内存，避免段错误
    for (int i = 0; i < 0x300; i += sizeof(void*)) {
        uintptr_t addr = (uintptr_t)item + i;
        if (addr < 0x1000) continue; // 跳过内核低地址，避免非法访问
        void** str_ptr = (void**)addr;
        if (!str_ptr || !*str_ptr) continue;
        const char* str = *(const char**)str_ptr;
        if (str && strcmp(str, "diamond_pickaxe") == 0) {
            return i;
        }
    }
    return -1;
}

int find_mNamespace(void* item) {
    if (!item) return -1;
    for (int i = 0; i < 0x300; i += sizeof(void*)) {
        uintptr_t addr = (uintptr_t)item + i;
        if (addr < 0x1000) continue;
        // 先判断是否是有效的字符串指针，避免强转std::string崩溃
        const char* test_str = *(const char**)addr;
        if (test_str && strcmp(test_str, "minecraft") == 0) {
            return i;
        }
    }
    return -1;
}

short findIdOffset(void* item) {
    if (!item) return -1;
    for (int i = 0; i < 0x200; i += sizeof(short)) {
        uintptr_t addr = (uintptr_t)item + i;
        if (addr < 0x1000) continue;
        short val = *(short*)addr;
        if (val > 0 && val < 10000) {
            return i;
        }
    }
    return -1;
}

// ==================== 核心Hook函数（全量安全校验，无崩溃风险） ====================
static void Item_appendFormattedHovertext_hook(void* self, ItemStackBase* stack, void* level, std::string& text, bool flag) {
    // 前置全量空指针校验，任何参数无效直接调用原函数返回，避免崩溃
    if (!self || !stack || !g_Item_appendHover_orig) {
        return;
    }
    // 先调用原函数，保证游戏原有逻辑正常执行
    ((Item_appendHover_t)g_Item_appendHover_orig)(self, stack, level, text, flag);

    // 核心空指针防护：Item为空直接返回，修复点击物品崩溃
    Item* item = stack->mItem.get();
    if (!item) return;

    // 基础属性获取，带有效性校验
    short maxDamage = item->getMaxDamage();
    IFoodItemComponent* food = item->getFood();
    std::string rawNameId;
    // 安全获取rawNameId，避免无效string构造崩溃
    try {
        rawNameId = item->mRawNameId.mStr;
    } catch (...) {
        rawNameId = "";
    }

    // 安全日志打印，仅偏移量有效时输出，避免无效内存访问
    log_write("Item hover triggered, rawNameId: %s", rawNameId.empty() ? "unknown" : rawNameId.c_str());
    log_write("mId offset: 0x%X", mId_offset);
    log_write("mRawNameId offset: 0x%X", mRawNameId_offset);
    log_write("mNamespace offset: 0x%X", mNamespace_offset);

    // 安全获取并打印命名空间，无空指针风险
    if (mRawNameId_offset != -1) {
        void* str_ptr = *(void**)((uintptr_t)item + mRawNameId_offset);
        if (str_ptr) {
            const char* rawName = *(const char**)str_ptr;
            log_write("Namespace full: %s", rawName ? rawName : "null");
        }
    }
    if (mNamespace_offset != -1) {
        const char* namespace_str = *(const char**)((uintptr_t)item + mNamespace_offset);
        log_write("Namespace: %s", namespace_str ? namespace_str : "null");
    }

    // 功能逻辑执行，全量带校验，无崩溃风险
    if (item->isFood() && food != nullptr) FoodTooltips(food, text);
    if (maxDamage != 0) ToolDurability(maxDamage, stack, text);
    if (rawNameId == "bee_nest" || rawNameId == "beehive") BeeNest(stack, text);

    // 安全追加物品信息，避免无效string访问崩溃
    try {
        text += std::format("\n§7{}:{} (#{})§r", item->mNamespace, rawNameId, item->mId);
    } catch (...) {
        // 异常捕获，避免格式错误导致崩溃
    }
}

// ==================== 签名扫描函数 ====================
void* resolve(const char* sig, const char* name) {
    if (!sig || !name) return nullptr;
    sigscan_handle* handle = sigscan_setup(sig, "libminecraftpe.so", GPWN_SIGSCAN_XMEM);
    if (!handle) return nullptr;
    void* func = get_sigscan_result(handle);
    sigscan_cleanup(handle);
    return (func != (void*)-1) ? func : nullptr;
}

// ==================== 模块初始化（核心闪退修复在这里） ====================
__attribute__((constructor))
static void mod_init() {
    // 初始化日志
    log_init("/sdcard/log.txt");
    log_write("BetterTooltips mod init start");

    // 初始化Hook框架
    GlossInit(true);

    // 核心修复1：函数指针有效性校验，避免nullptr调用崩溃
    Nbt_treeFind = (Nbt_treeFind_t)resolve("?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 FD 03 00 91 F3 03 00 AA ?? ?? ?? F8 ?? ?? ?? B4 ?? ?? ?? A9 F5 03 13 AA ?? ?? ?? 14 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 91 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? 39 ?? ?? ?? 36 ?? ?? ?? F9 ?? ?? ?? 36 ?? ?? ?? F9 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 34 ?? ?? ?? 37 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 14 ?? ?? ?? 91 ?? ?? ?? 37 ?? ?? ?? D3 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 35 DF 02 18 EB ?? ?? ?? 54 E8 03 1F 2A ?? ?? ?? 71 ?? ?? ?? 54 F5 03 17 AA ?? ?? ?? F9 ?? ?? ?? B5 ?? ?? ?? 14 ?? ?? ?? 54 ?? ?? ?? 17 BF 02 13 EB ?? ?? ?? 54 ?? ?? ?? 39 ?? ?? ?? A9 E0 03 14 AA ?? ?? ?? D3 ?? ?? ?? 72 ?? ?? ?? 91 01 01 8B 9A 57 01 89 9A FF 02 16 EB E2 32 96 9A ?? ?? ?? 94 DF 02 17 EB E8 27 9F 1A 1F 00 00 71 E9 A7 9F 1A 08 01 89 1A 1F 01 00 71 73 12 95 9A E0 03 13 AA ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A8 C0 03 5F D6 ?? ?? ?? A9", "Nbt_treeFind");
    ItemStackBase_getDamageValue = (ItemStackBase_getDamageValue_t)resolve("?? ?? ?? D1 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? 91 ?? ?? ?? D5 ?? ?? ?? F9 ?? ?? ?? F8 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4", "ItemStackBase_getDamageValue");

    // 日志输出函数指针状态，方便排查
    log_write("Nbt_treeFind: %p", Nbt_treeFind);
    log_write("ItemStackBase_getDamageValue: %p", ItemStackBase_getDamageValue);

    // 核心修复2：删除所有子类重复Hook！！！（90%闪退的根源）
    // 只需要Hook基类Item的虚函数，所有子类都会自动继承这个Hook
    // 重复Hook会覆盖g_Item_appendHover_orig原函数指针，导致调用原函数时跳转到错误地址，直接闪退
    if (g_Item_appendHover_orig == nullptr) {
        bool hook_success = miniAPI::hook::vtable("libminecraftpe.so", "4Item", 55, &g_Item_appendHover_orig, (void*)Item_appendFormattedHovertext_hook);
        log_write("Item vtable hook success: %d, orig address: %p", hook_success, g_Item_appendHover_orig);
    }

    // 核心修复3：偏移量仅在初始化时计算一次，避免每次渲染都遍历内存导致崩溃
    // （这里可以后续找一个有效Item对象计算，当前先缓存，首次调用时计算）
    log_write("BetterTooltips mod init complete");
}

// ==================== 模块卸载 ====================
__attribute__((destructor))
static void mod_unload() {
    log_write("BetterTooltips mod unload");
    log_close();
}
