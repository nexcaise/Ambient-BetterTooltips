#pragma once

class IFoodItemComponent {
public:
    virtual ~IFoodItemComponent() = default;

    virtual int getNutrition() const = 0;

    virtual float getSaturationModifier() const = 0;
};