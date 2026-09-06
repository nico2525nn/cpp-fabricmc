package net.minecraft.entity.boss;

import net.minecraft.text.Text;

/** Shared boss-bar value. */
public class BossBar {
    public enum Color { PINK, BLUE, RED, GREEN, YELLOW, PURPLE, WHITE }
    public enum Style { PROGRESS, NOTCHED_6, NOTCHED_10, NOTCHED_12, NOTCHED_20 }
    private final Text name;
    private int value = 1;
    private int maxValue = 1;
    public BossBar(Text name) { this.name = name == null ? Text.empty() : name; }
    public Text getName() { return name; }
    public int getValue() { return value; }
    public void setValue(int value) { this.value = value; }
    public int getMaxValue() { return maxValue; }
    public void setMaxValue(int value) { this.maxValue = value; }
}
