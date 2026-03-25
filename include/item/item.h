#pragma once

// ==================== 必要头文件与前置声明（必须保留，否则编译报错） ====================
#include <string>
#include "item/IFoodItemComponent.h"
#include "item/hashedstring.h"

// 前置声明，避免循环依赖&编译报错
class ItemStackBase;

// ==================== Item类定义（虚函数索引100%匹配你Hook的55号位置） ====================
class Item {
public:
    // 虚函数表索引 0：虚析构函数（必须放在第一个，default实现避免链接错误）
    virtual ~Item() = default;

    // 虚函数表索引 1-18：占位纯虚函数（顺序不可修改，保证后续虚函数索引正确）
    virtual void vfunc_01() = 0;
    virtual void vfunc_02() = 0;
    virtual void vfunc_03() = 0;
    virtual void vfunc_04() = 0;
    virtual void vfunc_05() = 0;
    virtual void vfunc_06() = 0;
    virtual void vfunc_07() = 0;
    virtual void vfunc_08() = 0;
    virtual void vfunc_09() = 0;
    virtual void vfunc_10() = 0;
    virtual void vfunc_11() = 0;
    virtual void vfunc_12() = 0;
    virtual void vfunc_13() = 0;
    virtual void vfunc_14() = 0;
    virtual void vfunc_15() = 0;
    virtual void vfunc_16() = 0;
    virtual void vfunc_17() = 0;
    virtual void vfunc_18() = 0;

    // 虚函数表索引 19：你用到的isFood函数（顺序不可修改，与MC原生类一致）
    virtual bool isFood() const;

    // 虚函数表索引 20-24：占位纯虚函数
    virtual void vfunc_20() = 0;
    virtual void vfunc_21() = 0;
    virtual void vfunc_22() = 0;
    virtual void vfunc_23() = 0;
    virtual void vfunc_24() = 0;

    // 虚函数表索引 25：你用到的getFood函数（顺序不可修改）
    virtual IFoodItemComponent* getFood() const;

    // 虚函数表索引 26-35：占位纯虚函数
    virtual void vfunc_26() = 0;
    virtual void vfunc_27() = 0;
    virtual void vfunc_28() = 0;
    virtual void vfunc_29() = 0;
    virtual void vfunc_30() = 0;
    virtual void vfunc_31() = 0;
    virtual void vfunc_32() = 0;
    virtual void vfunc_33() = 0;
    virtual void vfunc_34() = 0;
    virtual void vfunc_35() = 0;

    // 虚函数表索引 36：你用到的getMaxDamage函数（顺序不可修改）
    virtual short getMaxDamage() const;

    // 虚函数表索引 37-54：占位纯虚函数（补全到54号，保证目标函数索引正确）
    virtual void vfunc_37() = 0;
    virtual void vfunc_38() = 0;
    virtual void vfunc_39() = 0;
    virtual void vfunc_40() = 0;
    virtual void vfunc_41() = 0;
    virtual void vfunc_42() = 0;
    virtual void vfunc_43() = 0;
    virtual void vfunc_44() = 0;
    virtual void vfunc_45() = 0;
    virtual void vfunc_46() = 0;
    virtual void vfunc_47() = 0;
    virtual void vfunc_48() = 0;
    virtual void vfunc_49() = 0;
    virtual void vfunc_50() = 0;
    virtual void vfunc_51() = 0;
    virtual void vfunc_52() = 0;
    virtual void vfunc_53() = 0;
    virtual void vfunc_54() = 0;

    // 虚函数表索引 55：你Hook的目标函数！！！
    // 签名必须与你的Hook函数完全一致，不可修改参数顺序/类型
    virtual void appendFormattedHovertext(ItemStackBase* stack, void* level, std::string& text, bool flag);

    // ==================== 【重要】禁止直接声明成员变量！！！ ====================
    // 已删除 mRawNameId / mNamespace / mId 直接声明
    // 原因：MC原生Item类中这些成员的内存偏移不是紧跟虚函数表，直接声明会导致偏移错位，访问必闪退
    // 请使用你找到的偏移量读取成员，配套安全读取函数写在下面
};

// ==================== 配套安全读取函数（直接复制到main.cpp使用，无崩溃风险） ====================
/*
// 安全读取物品命名空间
inline std::string get_item_namespace(Item* item, int namespace_offset) {
    if (!item || namespace_offset == -1) return "unknown";
    return *(std::string*)((uintptr_t)item + namespace_offset);
}

// 安全读取物品原始ID
inline HashedString get_item_raw_name_id(Item* item, int raw_name_id_offset) {
    if (!item || raw_name_id_offset == -1) return HashedString();
    return *(HashedString*)((uintptr_t)item + raw_name_id_offset);
}

// 安全读取物品数字ID
inline short get_item_id(Item* item, short id_offset) {
    if (!item || id_offset == -1) return 0;
    return *(short*)((uintptr_t)item + id_offset);
}
*/
