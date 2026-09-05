package net.minecraft.util;

public enum Arm {
    LEFT, RIGHT;

    public Arm getOpposite() { return this == LEFT ? RIGHT : LEFT; }
}
