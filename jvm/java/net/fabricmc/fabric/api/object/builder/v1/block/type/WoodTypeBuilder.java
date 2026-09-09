package net.fabricmc.fabric.api.object.builder.v1.block.type;

import net.minecraft.block.BlockSetType;
import net.minecraft.sound.BlockSoundGroup;
import net.minecraft.block.WoodType;
import net.minecraft.sound.SoundEvent;
import net.minecraft.util.Identifier;

/** Builder matching the 1.21.4 wood/sign/fence-gate sound policy. */
public final class WoodTypeBuilder {
    private BlockSoundGroup soundGroup = BlockSoundGroup.WOOD;
    private BlockSoundGroup hangingSignSoundGroup = BlockSoundGroup.WOOD;
    private SoundEvent fenceGateCloseSound, fenceGateOpenSound;
    public WoodTypeBuilder() { }
    public WoodTypeBuilder soundGroup(BlockSoundGroup value) { if (value != null) soundGroup = value; return this; }
    public WoodTypeBuilder hangingSignSoundGroup(BlockSoundGroup value) { if (value != null) hangingSignSoundGroup = value; return this; }
    public WoodTypeBuilder fenceGateCloseSound(SoundEvent value) { fenceGateCloseSound = value; return this; }
    public WoodTypeBuilder fenceGateOpenSound(SoundEvent value) { fenceGateOpenSound = value; return this; }
    public static WoodTypeBuilder copyOf(WoodTypeBuilder value) { return value == null ? new WoodTypeBuilder() : value; }
    public static WoodTypeBuilder copyOf(WoodType value) {
        return new WoodTypeBuilder().soundGroup(value.soundType()).hangingSignSoundGroup(value.hangingSignSoundType())
            .fenceGateCloseSound(value.fenceGateCloseSound()).fenceGateOpenSound(value.fenceGateOpenSound());
    }
    public WoodType register(Identifier id, BlockSetType blockSetType) { return WoodType.register(id, build(id, blockSetType)); }
    public WoodType build(Identifier id, BlockSetType blockSetType) {
        return new WoodType(id == null ? "custom" : id.toString(), blockSetType, soundGroup,
            hangingSignSoundGroup, fenceGateCloseSound, fenceGateOpenSound);
    }
}
