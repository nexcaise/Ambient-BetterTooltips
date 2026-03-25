#pragma once
#include <cstdint>
#include <string>
#include "item/sharedptr.h"

// 前置声明，避免循环依赖
class Item;
class CompoundTag;

class ItemStackBase {
public:
    // ==================== 虚函数表（索引严格对应MC原生类，不可修改顺序） ====================
    // 虚函数表索引 0：虚析构函数（必须放在第一个）
    virtual ~ItemStackBase();

    // 虚函数表索引 1-4：reinit系列虚函数
    virtual void reinit_item(const Item&, int, int);
    virtual void reinit_block(const void*, int);
    virtual void reinit_name(const void*, int, int);
    virtual void setNull(void*);

    // 虚函数表索引 5-6：字符串转换虚函数
    virtual std::string toString() const;
    virtual std::string toDebugString() const;

    // ==================== 成员变量（偏移100%匹配MC原生arm64架构） ====================
    // 注意：开头8字节是编译器自动添加的vptr，这里不声明，避免偏移错位
    WeakPtr<Item>   mItem;          // 偏移 0x8-0x18（arm64下WeakPtr固定16字节）
    CompoundTag*    mUserData;      // 偏移 0x18-0x20（8字节指针）
    uint8_t         _pad_20[0x88 - 0x20]; // 填充到游戏原生固定大小0x88

    // ==================== 构造函数与运算符重载 ====================
    ItemStackBase();
    ItemStackBase(const ItemStackBase&);
    ItemStackBase& operator=(const ItemStackBase&);
};

// 全局函数指针声明（与你的代码完全兼容）
using ItemStackBase_getDamageValue_t = short (*)(ItemStackBase*);
extern ItemStackBase_getDamageValue_t ItemStackBase_getDamageValue;

// 静态断言，强制校验类大小与游戏原生一致，编译时就能发现偏移错误
static_assert(sizeof(ItemStackBase) == 0x88, "ItemStackBase size mismatch! Must be 0x88 for arm64");
