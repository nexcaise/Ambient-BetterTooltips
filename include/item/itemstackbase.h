#pragma once
#include <cstdint>
#include <string>
#include "item/sharedptr.h"

// 前置声明，避免循环依赖
class Item;
class CompoundTag;

class ItemStackBase {
public:
    // ==================== 虚函数表（顺序不可修改，与MC原生类对应） ====================
    virtual ~ItemStackBase();
    virtual void reinit_item(const Item&, int, int);
    virtual void reinit_block(const void*, int);
    virtual void reinit_name(const void*, int, int);
    virtual void setNull(void*);
    virtual std::string toString() const;
    virtual std::string toDebugString() const;

    // ==================== 成员变量（自动适配内存布局，无偏移错误） ====================
    WeakPtr<Item>   mItem;
    CompoundTag*    mUserData;

    // 【核心修复】自动计算填充长度，强制让类总大小严格等于0x88（136字节）
    // 自动适配vptr、成员变量的大小，不再手动写死偏移，彻底解决大小不匹配问题
    uint8_t _pad[0x88 - sizeof(void*) - sizeof(WeakPtr<Item>) - sizeof(CompoundTag*)];

    // ==================== 构造函数与运算符重载 ====================
    ItemStackBase();
    ItemStackBase(const ItemStackBase&);
    ItemStackBase& operator=(const ItemStackBase&);
};

// 全局函数指针声明（与你的代码完全兼容）
using ItemStackBase_getDamageValue_t = short (*)(ItemStackBase*);
extern ItemStackBase_getDamageValue_t ItemStackBase_getDamageValue;

// 编译时强制校验，有任何大小错误会直接在编译阶段暴露，避免运行闪退
static_assert(sizeof(WeakPtr<Item>) == 8, "WeakPtr size mismatch! Must be 8 bytes for arm64-v8a");
static_assert(sizeof(ItemStackBase) == 0x88, "ItemStackBase size mismatch! Must be 0x88 for arm64-v8a");
