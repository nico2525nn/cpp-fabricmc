package net.minecraft.sound;

import net.minecraft.util.Identifier;

/** Named sound event value used by entity and world APIs. */
public class SoundEvent {
    private final Identifier id;
    private final Float fixedRange;

    public SoundEvent(Identifier id) { this(id, null); }
    public SoundEvent(Identifier id, Float fixedRange) {
        this.id = id == null ? Identifier.of("minecraft", "empty") : id;
        this.fixedRange = fixedRange;
    }
    public static SoundEvent of(Identifier id) { return new SoundEvent(id); }
    public static SoundEvent of(Identifier id, Float fixedRange) { return new SoundEvent(id, fixedRange); }
    public Identifier id() { return id; }
    public java.util.Optional<Float> fixedRange() { return java.util.Optional.ofNullable(fixedRange); }
    public float getDistanceToTravel(float volume) { return fixedRange == null ? 16.0f * volume : fixedRange; }
}
