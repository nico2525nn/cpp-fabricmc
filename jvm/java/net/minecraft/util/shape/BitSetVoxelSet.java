package net.minecraft.util.shape;

import java.util.BitSet;

/** Bit-backed voxel set used by vanilla shape combination code. */
public class BitSetVoxelSet extends VoxelSet {
    public final BitSet storage;
    public int minX;
    public int minY;
    public int minZ;
    public int maxX;
    public int maxY;
    public int maxZ;
    /** Canonical field spellings used by the 1.21.4 named shadow. */
    public int xMin;
    public int yMin;
    public int zMin;
    public int xMax;
    public int yMax;
    public int zMax;

    public BitSetVoxelSet(int sizeX, int sizeY, int sizeZ) {
        super(sizeX, sizeY, sizeZ);
        this.storage = new BitSet(Math.max(0, sizeX * sizeY * sizeZ));
        this.maxX = sizeX; this.maxY = sizeY; this.maxZ = sizeZ;
        this.xMin = minX; this.yMin = minY; this.zMin = minZ;
        this.xMax = maxX; this.yMax = maxY; this.zMax = maxZ;
    }

    public BitSetVoxelSet(VoxelSet other) {
        this(other == null ? 0 : other.getXSize(), other == null ? 0 : other.getYSize(),
            other == null ? 0 : other.getZSize());
        if (other != null)
            for (int x = 0; x < sizeX; ++x) for (int y = 0; y < sizeY; ++y)
                for (int z = 0; z < sizeZ; ++z) if (other.contains(x, y, z)) set(x, y, z, false);
    }

    @Override public boolean contains(int x, int y, int z) {
        return x >= 0 && x < sizeX && y >= 0 && y < sizeY && z >= 0 && z < sizeZ
            && storage.get(index(x, y, z));
    }

    @Override public boolean inBoundsAndContains(int x, int y, int z) { return contains(x, y, z); }

    @Override public void set(int x, int y, int z) { set(x, y, z, true); }
    public void set(int x, int y, int z, boolean updateBounds) {
        if (x < 0 || x >= sizeX || y < 0 || y >= sizeY || z < 0 || z >= sizeZ) return;
        storage.set(index(x, y, z));
        if (updateBounds) {
            minX = Math.min(minX, x); minY = Math.min(minY, y); minZ = Math.min(minZ, z);
            maxX = Math.max(maxX, x + 1); maxY = Math.max(maxY, y + 1); maxZ = Math.max(maxZ, z + 1);
            xMin = minX; yMin = minY; zMin = minZ;
            xMax = maxX; yMax = maxY; zMax = maxZ;
        }
    }

    public int getIndex(int x, int y, int z) { return index(x, y, z); }
    private int index(int x, int y, int z) { return (y * sizeZ + z) * sizeX + x; }
}
