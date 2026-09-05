package net.minecraft.entity.player;

public final class HungerManager {
    private int foodLevel = 20;
    private float saturationLevel = 5.0f;
    private int foodTickTimer;
    public int getFoodLevel() { return foodLevel; }
    public void setFoodLevel(int value) { foodLevel = Math.max(0, Math.min(20, value)); }
    public float getSaturationLevel() { return saturationLevel; }
    public void setSaturationLevel(float value) { saturationLevel = Math.max(0.0f, value); }
    public int getFoodTickTimer() { return foodTickTimer; }
    public void add(int food, float saturation) { setFoodLevel(foodLevel + food); setSaturationLevel(saturationLevel + food * saturation * 2.0f); }
}
