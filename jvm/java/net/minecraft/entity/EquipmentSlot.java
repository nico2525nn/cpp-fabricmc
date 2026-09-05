package net.minecraft.entity;

public enum EquipmentSlot {
    MAINHAND(Type.HAND, 0, "mainhand"), OFFHAND(Type.HAND, 1, "offhand"),
    FEET(Type.ARMOR, 0, "feet"), LEGS(Type.ARMOR, 1, "legs"),
    CHEST(Type.ARMOR, 2, "chest"), HEAD(Type.ARMOR, 3, "head");
    private final Type type;
    private final int entityId;
    private final String name;
    EquipmentSlot(Type type, int entityId, String name) { this.type = type; this.entityId = entityId; this.name = name; }
    public Type getType() { return type; }
    public int getEntitySlotId() { return entityId; }
    public String getName() { return name; }
    public enum Type { HAND, ARMOR }
}
