package net.minecraft.component.type;

/** Bounded value view of the 1.21.4 food component. */
public class FoodComponent {
    private final int hunger;
    private final float saturation;
    private final boolean alwaysEdible;

    public FoodComponent(int hunger, float saturation) {
        this(hunger, saturation, false);
    }

    public FoodComponent(int hunger, float saturation, boolean alwaysEdible) {
        this.hunger = hunger;
        this.saturation = saturation;
        this.alwaysEdible = alwaysEdible;
    }

    public int nutrition() { return hunger; }
    public int getNutrition() { return hunger; }
    public float saturationModifier() { return saturation; }
    public float getSaturationModifier() { return saturation; }
    public boolean isAlwaysEdible() { return alwaysEdible; }

    public static class Builder {
        private int hunger;
        private float saturation;
        private boolean alwaysEdible;
        public Builder nutrition(int value) { hunger = value; return this; }
        public Builder saturationModifier(float value) { saturation = value; return this; }
        public Builder alwaysEdible() { alwaysEdible = true; return this; }
        public FoodComponent build() { return new FoodComponent(hunger, saturation, alwaysEdible); }
    }
}
