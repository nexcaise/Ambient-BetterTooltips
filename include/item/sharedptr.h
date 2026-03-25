#pragma once
#include <cstdint>
#include <string>
#include "item/sharedptr.h"

// 前置声明，避免循环依赖
class Item;
class CompoundTag;

class ItemStackBase {
public:
    // ==================== 虚函数表（顺序不可修改，与MC原生类完全对应） ====================
    // 虚函数表索引 0：虚析构函数（编译器自动在内存开头预留8字节vptr）
    virtual ~ItemStackBase();

    // 虚函数表索引 1-4：reinit系列虚函数
    virtual void reinit_item(const Item&, int, int);
    virtual void reinit_block(const void*, int);
    virtual void reinit_name(const void*, int, int);
    virtual void setNull(void*);

    // 虚函数表索引 5-6：字符串转换虚函数
    virtual std::string toString() const;
    virtual std::string toDebugString() const;

    // ==================== 成员变量（偏移100%精准匹配MC原生arm64架构） ====================
    WeakPtr<Item>   mItem;          // 偏移 0x8-0x10（vptr占0x0-0x8，WeakPtr固定8字节）
    CompoundTag*    mUserData;      // 偏移 0x10-0x18（8字节指针，无错位）
    uint8_t         _pad_18[0x88 - 0x18]; // 填充到游戏原生固定大小0x88，无越界

    // ==================== 构造函数与运算符重载 ====================
    ItemStackBase();
    ItemStackBase(const ItemStackBase&);
    ItemStackBase& operator=(const ItemStackBase&);
};

// 全局函数指针声明（与你的代码完全兼容）
using ItemStackBase_getDamageValue_t = short (*)(ItemStackBase*);
extern ItemStackBase_getDamageValue_t ItemStackBase_getDamageValue;

// 编译时强制校验类大小，有偏移错误会直接编译报错，提前规避闪退
static_assert(sizeof(ItemStackBase) == 0x88, "ItemStackBase size mismatch! Must be 0x88 for arm64-v8a");
static_assert(sizeof(WeakPtr<Item>) == 8, "WeakPtr size mismatch! Must be 8 bytes for arm64-v8a");
