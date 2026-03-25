#pragma once

class IFoodItemComponent {
public:
    virtual ~IFoodItemComponent();
    
    // Virtual function table layout - may vary by MCBE version
    // These are the common methods for food items
    virtual int getNutrition() const;
    virtual float getSaturationModifier() const;
    virtual bool isAlwaysEdible() const;
    virtual void* getEatCallback() const;
};
