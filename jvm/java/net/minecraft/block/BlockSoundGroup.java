package net.minecraft.block;

public final class BlockSoundGroup {
    public static final BlockSoundGroup STONE = new BlockSoundGroup("stone");
    public static final BlockSoundGroup WOOD = new BlockSoundGroup("wood");
    public static final BlockSoundGroup GRASS = new BlockSoundGroup("grass");
    private final String name;
    public BlockSoundGroup(String name) { this.name = name == null ? "default" : name; }
    public String name() { return name; }
    @Override public String toString() { return name; }
}
