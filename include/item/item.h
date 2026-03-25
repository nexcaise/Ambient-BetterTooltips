#pragma once

#include "item/IFoodItemComponent.h"

// NOTE: Item is a large game class with 100+ virtual functions.
// Data members (mRawNameId, mNamespace, mId) are at version-dependent offsets.
// Access them via runtime offset discovery (see main.cpp).
// Virtual function indices are also version-dependent and discovered at runtime.
class Item {
public:
    virtual ~Item();
    
    // Placeholder virtual functions - actual indices discovered at runtime
    // This layout is approximate and may not match all MCBE versions
    
    // Common Item interface methods (indices may vary)
    virtual void* getDescriptionId() const;          // Usually returns name
    virtual void* getRawNameId() const;              // Usually returns HashedString
    
    // Use runtime discovery for important methods
    // These are defined for compilation but actual indices are discovered at runtime
    virtual bool isFood() const;                     // Varies by version
    virtual IFoodItemComponent* getFood() const;     // Varies by version
    virtual short getMaxDamage() const;              // Varies by version
    
    // Generic placeholder for vtable padding - Item has many more virtuals
    virtual void vfunc_placeholder() = 0;
};

// ArmorItem class - inherits from Item
// Virtual function indices are discovered at runtime
class ArmorItem : public Item {
public:
    virtual int getArmorValue() const;
    virtual int getToughnessValue() const;
};
