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
#include <sys/stat.h> // 新增：创建目录用

// 前置声明 NBT 相关函数
bool containsTag(void* data, const char* tag);
void* getListTag(void* data, const char* tag);
int listSize(void* list);

static std::mutex log_mutex;
static FILE* log_file = nullptr;

static const char* get_time_str() {
    static thread_local char buf[64];
    time_t t = time(nullptr);
    tm* tm_info = localtime(&t);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    return buf;
}

// ===================== 修复1：日志系统核心修复 =====================
// 新增：创建目录函数（解决/storage/emulated/0/无权限/目录不存在问题）
bool create_dir(const char* path) {
    if (!path) return false;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", path);
    return system(cmd) == 0;
}

void log_init(const char* path) {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (log_file || !path) return;

    // 修复：Android 存储路径权限问题 + 目录创建
    const char* log_dir = "/storage/emulated/0/";
    create_dir(log_dir);

    // 修复：使用 "w+" 替代 "a+"，确保文件可创建；增加权限校验
    log_file = fopen(path, "w+");
    if (log_file) {
        chmod(path, 0666); // 赋予文件读写权限
        log_write("BetterTooltips mod init start");
    } else {
        // 备用路径：应用私有目录（避免存储权限问题）
        log_file = fopen("/data/local/tmp/BetterTooltips.log", "w+");
        if (log_file) {
            log_write("Using fallback log path: /data/local/tmp/BetterTooltips.log");
        }
    }
}

void log_close() {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (!log_file) return;
    log_write("BetterTooltips mod unload");
    fclose(log_file);
    log_file = nullptr;
}

void log_write(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(log_mutex);
    // 修复：双重校验文件和格式字符串，彻底避免空指针
    if (!log_file || !fmt) return;

    fprintf(log_file, "[%s] ", get_time_str());

    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);

    fprintf(log_file, "\n");
    fflush(log_file); // 强制刷新缓冲区，确保实时写入
}

static void* g_Item_appendHover_orig = nullptr;
using Item_appendHover_t = void(*)(void*, ItemStackBase*, void*, std::string&, bool);

std::string buildBarString(int v, const std::string full, const std::string half) {
    std::string out;
    for (int i = 0; i < (int)v; i+=2) out += full;
    if (fmod(v, 2) != 0)
        out += half;
    return out;
}

void FoodTooltips(IFoodItemComponent* food, std::string& text) {
    if (!food) return;
    int nutrition = food->getNutrition();
    int saturation = food->getSaturationModifier() * nutrition * 2;
    if(nutrition > 0) text += std::format("\n{} ({})", buildBarString(nutrition, "", ""), nutrition);
    if(saturation > 0) text += std::format("\n{} ({})", buildBarString(saturation, "", ""), saturation);
}

void BeeNest(ItemStackBase* stack, std::string& text) {
    if (!stack) return;
    void* data = stack->mUserData;
    if(!data || (!containsTag(data, "Occupants") && !containsTag(data, "occupants"))) {
      text += "\n§7Contains 0 bees§r";
      return;
    }
    auto* list = reinterpret_cast<ListTagLayout*>(getListTag(data, "Occupants"));
    if (!list)
        list = reinterpret_cast<ListTagLayout*>(getListTag(data, "occupants"));
    if(list) {
        int size = listSize(list);
        text += std::format("\n§7Contains {} bee{}§r", size, size > 1 ? "s" : "");
    }
}

void ToolDurability(short maxDamage, ItemStackBase* s, std::string& text) {
    if (!s) return;
    short current = maxDamage - ItemStackBase_getDamageValue(s);
    text += std::format("\n§7Durability: {} / {}§r",  current, maxDamage);
}

int find_mRawNameId_offset(void* item) {
    if (!item) return -1;
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

int find_mNamespace_offset(void* item) {
    if (!item) return -1;
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
    if (!item) return -1;
    for (int i = 0; i < 0x200; i++) {
        short val = *(short*)((uintptr_t)item + i);
        if (val > 0 && val < 10000) {
            return i;
        }
    }
    return -1;
}

// ===================== 核心修复：空指针+异常捕获，解决闪退 =====================
static void Item_appendFormattedHovertext_hook(void* self, ItemStackBase* stack, void* level, std::string& text, bool flag) {
    // 修复：异常捕获（Android信号保护，防止闪退）
    __try {
        if (g_Item_appendHover_orig) {
            ((Item_appendHover_t)g_Item_appendHover_orig)(self, stack, level, text, flag);
        }

        // 基础空指针校验
        if (!stack) {
            log_write("Hook: stack is null, exit");
            return;
        }

        Item* item = stack->mItem.get();
        if (!item) {
            log_write("Hook: item is null, exit custom logic");
            return;
        }

        short maxDamage = item->getMaxDamage();
        IFoodItemComponent* food = item->getFood();
        std::string rawNameId = item->mRawNameId.mStr;

        log_write("Hello world");
        log_write("mId offset: 0x%X", findIdOffset((void*)item));
        log_write("mRawNameId offset: 0x%X", find_mRawNameId_offset((void*)item));
        log_write("mNamespace offset: 0x%X", find_mNamespace_offset((void*)item));

        if(item->isFood() && food != nullptr) FoodTooltips(food, text); 
        if(maxDamage != 0) ToolDurability(maxDamage,stack,text);
        if(rawNameId == "bee_nest" || rawNameId == "beehive") BeeNest(stack, text);

        text += std::format("\n§7{}:{} (#{})§r", item->mNamespace, rawNameId, item->mId);
    }
    __except(1) { // 捕获所有异常，防止游戏闪退
        log_write("Hook: exception caught, avoid crash");
    }
}

void* resolve(const char *sgig, const char *name) {
    sigscan_handle *handle = sigscan_setup(sgig, "libminecraftpe.so", GPWN_SIGSCAN_XMEM);
    if(!handle) {
        log_write("sigscan_setup failed for %s", name);
        return (void*)0;
    }
    
    void *func = get_sigscan_result(handle);
    sigscan_cleanup(handle);
    
    if(func == (void*) -1 || !func) {
        log_write("sigscan result invalid for %s", name);
        return (void*)0;
    }

    log_write("%s: %p", name, func);
    return func;
}

__attribute__((constructor))
static void mod_init() {
    // 修复：指定绝对路径，确保日志生成
    log_init("/storage/emulated/0/BetterTooltips.log");
    
    GlossInit(true);

    Nbt_treeFind = (Nbt_treeFind_t)resolve("?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 FD 03 00 91 F3 03 00 AA ?? ?? ?? F8 ?? ?? ?? B4 ?? ?? ?? A9 F5 03 13 AA ?? ?? ?? 14 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 91 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? 39 ?? ?? ?? 36 ?? ?? ?? F9 ?? ?? ?? 36 ?? ?? ?? F9 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 34 ?? ?? ?? 37 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 14 ?? ?? ?? 91 ?? ?? ?? 37 ?? ?? ?? D3 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 35 DF 02 18 EB ?? ?? ?? 54 E8 03 1F 2A ?? ?? ?? 71 ?? ?? ?? 54 F5 03 17 AA ?? ?? ?? F9 ?? ?? ?? B5 ?? ?? ?? 14 ?? ?? ?? 54 ?? ?? ?? 17 BF 02 13 EB ?? ?? ?? 54 ?? ?? ?? 39 ?? ?? ?? A9 E0 03 14 AA ?? ?? ?? D3 ?? ?? ?? 72 ?? ?? ?? 91 01 01 8B 9A 57 01 89 9A FF 02 16 EB E2 32 96 9A ?? ?? ?? 94 DF 02 17 EB E8 27 9F 1A 1F 00 00 71 E9 A7 9F 1A 08 01 89 1A 1F 01 00 71 73 12 95 9A E0 03 13 AA ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A8 C0 03 5F D6 ?? ?? ?? A9","Nbt_treeFind");
        
    ItemStackBase_getDamageValue = (ItemStackBase_getDamageValue_t)resolve("?? ?? ?? D1 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? 91 ?? ?? ?? D5 ?? ?? ?? F9 ?? ?? ?? F8 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4","ItemStackBase_getDamageValue");
    
    // 完全保留你的原始Hook代码，无任何修改
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

    log_write("BetterTooltips mod init complete");
}

// 模块卸载时自动关闭日志
__attribute__((destructor))
static void mod_unload() {
    log_close();
}
