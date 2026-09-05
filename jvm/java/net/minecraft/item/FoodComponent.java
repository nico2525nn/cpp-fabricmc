package net.minecraft.item;

public final class FoodComponent {
    private final int hunger;
    private final float saturation;
    private final boolean alwaysEdible;
    public FoodComponent(int hunger, float saturation) { this(hunger, saturation, false); }
    public FoodComponent(int hunger, float saturation, boolean alwaysEdible) {
        this.hunger = hunger; this.saturation = saturation; this.alwaysEdible = alwaysEdible;
    }
    public int nutrition() { return hunger; }
    public int getNutrition() { return hunger; }
    public float saturationModifier() { return saturation; }
    public float getSaturationModifier() { return saturation; }
    public boolean isAlwaysEdible() { return alwaysEdible; }
}
