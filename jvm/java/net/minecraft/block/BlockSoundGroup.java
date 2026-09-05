package net.minecraft.block;

/** @deprecated use {@link net.minecraft.sound.BlockSoundGroup}. */
@Deprecated
public class BlockSoundGroup extends net.minecraft.sound.BlockSoundGroup {
    public static final BlockSoundGroup STONE = new BlockSoundGroup("stone");
    public static final BlockSoundGroup WOOD = new BlockSoundGroup("wood");
    public static final BlockSoundGroup GRASS = new BlockSoundGroup("grass");
    public BlockSoundGroup(String name) { super(name); }
}
