#pragma once

// 必须包含的头文件与前置声明
#include <string>
#include "item/IFoodItemComponent.h"
#include "item/hashedstring.h"

// 前置声明，避免循环依赖
class ItemStackBase;

// 偏移量全局声明（与main.cpp的缓存变量对应，安全读取成员）
extern int mRawNameId_offset;
extern int mNamespace_offset;
extern short mId_offset;

class Item {
public:
    // 虚函数表索引 0：虚析构函数
    virtual ~Item() = default;

    // 虚函数表索引 1-18：占位纯虚函数（顺序不可修改，保证索引正确）
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

    // 虚函数表索引 19：isFood函数
    virtual bool isFood() const;

    // 虚函数表索引 20-24：占位纯虚函数
    virtual void vfunc_20() = 0;
    virtual void vfunc_21() = 0;
    virtual void vfunc_22() = 0;
    virtual void vfunc_23() = 0;
    virtual void vfunc_24() = 0;

    // 虚函数表索引 25：getFood函数
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

    // 虚函数表索引 36：getMaxDamage函数
    virtual short getMaxDamage() const;

    // 虚函数表索引 37-54：占位纯虚函数（补全到54号，保证Hook索引正确）
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

    // 虚函数表索引 55：你Hook的目标函数，签名完全匹配
    virtual void appendFormattedHovertext(ItemStackBase* stack, void* level, std::string& text, bool flag);

    // ==================== 安全成员读取函数（解决编译报错，无偏移闪退） ====================
    inline HashedString& getRawNameId() const {
        return *(HashedString*)((uintptr_t)this + mRawNameId_offset);
    }

    inline std::string& getNamespace() const {
        return *(std::string*)((uintptr_t)this + mNamespace_offset);
    }

    inline short getId() const {
        return *(short*)((uintptr_t)this + mId_offset);
    }
};
