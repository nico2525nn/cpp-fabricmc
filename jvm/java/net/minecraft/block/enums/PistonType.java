package net.minecraft.block.enums;

/** Piston head state property values in 1.21.4. */
public enum PistonType {
    NORMAL("normal"), STICKY("sticky");
    private final String name;
    PistonType(String name) { this.name = name; }
    public String asString() { return name; }
    @Override public String toString() { return name; }
}
