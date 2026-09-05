package net.minecraft.world;

public final class WorldBorder {
    private double centerX, centerZ;
    private double size = 59999968.0;
    public double getCenterX() { return centerX; }
    public double getCenterZ() { return centerZ; }
    public void setCenter(double x, double z) { centerX = x; centerZ = z; }
    public double getSize() { return size; }
    public void setSize(double value) { size = Math.max(1.0, value); }
    public boolean contains(double x, double z) { return Math.abs(x - centerX) <= size / 2 && Math.abs(z - centerZ) <= size / 2; }
}
