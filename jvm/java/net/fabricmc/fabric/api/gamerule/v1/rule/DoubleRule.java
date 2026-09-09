package net.fabricmc.fabric.api.gamerule.v1.rule;

import net.minecraft.server.MinecraftServer;
import net.minecraft.world.GameRules;

/** A bounded floating-point Fabric game rule. */
public final class DoubleRule extends GameRules.Rule<DoubleRule>
        implements ValidateableRule {
    private double value;
    private final double minimum;
    private final double maximum;

    public DoubleRule(GameRules.Type<DoubleRule> type, double initialValue,
                      double minimum, double maximum) {
        super(type);
        if (minimum > maximum) throw new IllegalArgumentException("minimum > maximum");
        if (!Double.isFinite(initialValue) || initialValue < minimum || initialValue > maximum)
            throw new IllegalArgumentException("initial value outside range");
        this.value = initialValue;
        this.minimum = minimum;
        this.maximum = maximum;
    }

    @Override public String serialize() { return Double.toString(value); }
    @Override public int getCommandResult() { return (int) value; }
    /** Intermediary names retained for the official Fabric 1.21.4 ABI. */
    public String method_20779() { return serialize(); }
    public int method_20781() { return getCommandResult(); }
    @Override public void setValue(DoubleRule rule, MinecraftServer server) {
        if (rule != null) set(rule.value, server);
    }
    @Override public boolean validate(String input) {
        try {
            double parsed = Double.parseDouble(input);
            return Double.isFinite(parsed) && parsed >= minimum && parsed <= maximum;
        } catch (RuntimeException failure) {
            return false;
        }
    }
    @Override public void deserialize(String input) {
        if (!validate(input)) throw new IllegalArgumentException("invalid double gamerule: " + input);
        value = Double.parseDouble(input);
    }
    public double get() { return value; }
    public void set(double value, MinecraftServer server) {
        if (!Double.isFinite(value) || value < minimum || value > maximum)
            throw new IllegalArgumentException("value outside range");
        if (Double.doubleToLongBits(this.value) == Double.doubleToLongBits(value)) return;
        this.value = value;
        changed(server);
    }
    public void method_27337(GameRules.Rule<?> rule, MinecraftServer server) {
        if (rule instanceof DoubleRule other) setValue(other, server);
    }
    @Override public DoubleRule copy() {
        return new DoubleRule(type, value, minimum, maximum);
    }
    @Override public String toString() { return serialize(); }
}
