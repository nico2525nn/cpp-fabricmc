package net.minecraft.sound;

/**
 * Named sound group used by the 1.21.4 block settings ABI.
 *
 * The native engine currently keeps the sound event selection; this value
 * object exists so mods can construct and inspect the canonical Java type.
 */
public class BlockSoundGroup {
    public static final BlockSoundGroup STONE = new BlockSoundGroup("stone");
    public static final BlockSoundGroup WOOD = new BlockSoundGroup("wood");
    public static final BlockSoundGroup GRASS = new BlockSoundGroup("grass");

    private final String name;

    public BlockSoundGroup(String name) {
        this.name = name == null ? "default" : name;
    }

    public String name() { return name; }
    public String getName() { return name; }

    @Override public String toString() { return name; }
}
