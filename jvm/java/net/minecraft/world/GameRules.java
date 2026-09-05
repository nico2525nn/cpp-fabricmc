package net.minecraft.world;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public final class GameRules {
    public static final Key<BooleanRule> DO_DAYLIGHT_CYCLE = new Key<>("doDaylightCycle");
    public static final Key<BooleanRule> DO_MOB_SPAWNING = new Key<>("doMobSpawning");
    public static final Key<BooleanRule> KEEP_INVENTORY = new Key<>("keepInventory");
    public static final Key<IntRule> RANDOM_TICK_SPEED = new Key<>("randomTickSpeed");
    private final Map<Key<?>, Rule<?>> values = new ConcurrentHashMap<>();
    public GameRules() { values.put(DO_DAYLIGHT_CYCLE, new BooleanRule(true)); values.put(DO_MOB_SPAWNING, new BooleanRule(true)); values.put(KEEP_INVENTORY, new BooleanRule(false)); values.put(RANDOM_TICK_SPEED, new IntRule(3)); }
    @SuppressWarnings("unchecked") public <T extends Rule<T>> T get(Key<T> key) { return (T) values.get(key); }
    public static final class Key<T extends Rule<T>> { private final String id; public Key(String id) { this.id = id; } public String id() { return id; } @Override public String toString() { return id; } }
    public abstract static class Rule<T extends Rule<T>> { public abstract T copy(); }
    public static final class BooleanRule extends Rule<BooleanRule> { private boolean value; public BooleanRule(boolean value) { this.value = value; } public boolean get() { return value; } public void set(boolean value, Object server) { this.value = value; } @Override public BooleanRule copy() { return new BooleanRule(value); } }
    public static final class IntRule extends Rule<IntRule> { private int value; public IntRule(int value) { this.value = value; } public int get() { return value; } public void set(int value, Object server) { this.value = value; } @Override public IntRule copy() { return new IntRule(value); } }
}
