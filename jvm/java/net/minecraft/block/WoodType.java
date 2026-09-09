package net.minecraft.block;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.sound.BlockSoundGroup;
import net.minecraft.sound.SoundEvent;
import net.minecraft.util.Identifier;

/** Sign and fence-gate sound policy for a wood type. */
public final class WoodType {
    private static final Map<Identifier, WoodType> REGISTERED = new ConcurrentHashMap<>();
    private final String name;
    private final BlockSetType setType;
    private final BlockSoundGroup soundGroup;
    private final BlockSoundGroup hangingSignSoundGroup;
    private final SoundEvent fenceGateCloseSound;
    private final SoundEvent fenceGateOpenSound;
    public WoodType(String name, BlockSetType setType, BlockSoundGroup soundGroup,
                    BlockSoundGroup hangingSignSoundGroup, SoundEvent fenceGateCloseSound,
                    SoundEvent fenceGateOpenSound) {
        this.name = name == null ? "custom" : name; this.setType = setType;
        this.soundGroup = soundGroup; this.hangingSignSoundGroup = hangingSignSoundGroup;
        this.fenceGateCloseSound = fenceGateCloseSound; this.fenceGateOpenSound = fenceGateOpenSound;
    }
    public String name() { return name; }
    public BlockSetType setType() { return setType; }
    public BlockSoundGroup soundType() { return soundGroup; }
    public BlockSoundGroup hangingSignSoundType() { return hangingSignSoundGroup; }
    public SoundEvent fenceGateCloseSound() { return fenceGateCloseSound; }
    public SoundEvent fenceGateOpenSound() { return fenceGateOpenSound; }
    public static WoodType register(WoodType type) { return type; }
    public static WoodType register(Identifier id, WoodType type) { if (id != null && type != null) REGISTERED.putIfAbsent(id, type); return type; }
}
