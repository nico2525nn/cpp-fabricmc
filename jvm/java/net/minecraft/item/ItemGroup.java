package net.minecraft.item;

public final class ItemGroup {
    private final String id;
    public ItemGroup(String id) { this.id = id; }
    public String id() { return id; }
    @Override public String toString() { return id; }
}
