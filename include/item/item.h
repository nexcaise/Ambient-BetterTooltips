#pragma once

#include "item/IFoodItemComponent.h"
#include "item/hashedstring.h"

// NOTE: Item is a large game class. Data members (mRawNameId, mNamespace, mId)
// are at version-dependent offsets deep inside the class, NOT right after the vptr.
// Access them via runtime offset discovery (see main.cpp) instead of direct members.
class Item {
public:
    HashedString mRawNameId;
    std::string mNamespace;
    short mId;

public:
    virtual ~Item();
    virtual void vfunc1()  = 0;
    virtual void vfunc2()  = 0;
    virtual void vfunc3()  = 0;
    virtual void vfunc4()  = 0;
    virtual void vfunc5()  = 0;
    virtual void vfunc6()  = 0;
    virtual void vfunc7()  = 0;
    virtual void vfunc8()  = 0;
    virtual void vfunc9()  = 0;
    virtual void vfunc10() = 0;
    virtual void vfunc11() = 0;
    virtual void vfunc12() = 0;
    virtual void vfunc13() = 0;
    virtual void vfunc14() = 0;
    virtual void vfunc15() = 0;
    virtual void vfunc16() = 0;
    virtual void vfunc17() = 0;
    virtual void vfunc18() = 0;
    virtual bool isFood() const;                     // vtable[19]
    virtual void vfunc20() = 0;
    virtual void vfunc21() = 0;
    virtual void vfunc22() = 0;
    virtual void vfunc23() = 0;
    virtual void vfunc24() = 0;
    virtual IFoodItemComponent* getFood() const;     // vtable[25]
    virtual void vfunc26() = 0;
    virtual void vfunc27() = 0;
    virtual void vfunc28() = 0;
    virtual void vfunc29() = 0;
    virtual void vfunc30() = 0;
    virtual void vfunc31() = 0;
    virtual void vfunc32() = 0;
    virtual void vfunc33() = 0;
    virtual void vfunc34() = 0;
    virtual void vfunc35() = 0;
    virtual short getMaxDamage() const;              // vtable[36]
    virtual float getAttackDamage() const;           // vtable[37]
};


// ArmorItem class - inherits from Item
class ArmorItem : public Item {
public:
    virtual int getArmorValue() const;
    virtual int getToughnessValue() const;
};
