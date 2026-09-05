package net.minecraft.item;

/** @deprecated use {@link net.minecraft.component.type.FoodComponent}. */
@Deprecated
public class FoodComponent extends net.minecraft.component.type.FoodComponent {
    public FoodComponent(int hunger, float saturation) { super(hunger, saturation); }
    public FoodComponent(int hunger, float saturation, boolean alwaysEdible) {
        super(hunger, saturation, alwaysEdible);
    }
}
