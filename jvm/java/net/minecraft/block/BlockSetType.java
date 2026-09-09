package net.minecraft.block;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.sound.BlockSoundGroup;
import net.minecraft.sound.SoundEvent;
import net.minecraft.util.Identifier;

/** Sound and interaction policy shared by doors, buttons and pressure plates. */
public final class BlockSetType {
    public enum ActivationRule { EVERYTHING, MOBS }

    private static final Map<Identifier, BlockSetType> REGISTERED = new ConcurrentHashMap<>();
    private final String name;
    private final boolean openableByHand;
    private final boolean openableByWindCharge;
    private final boolean buttonActivatedByArrows;
    private final ActivationRule pressurePlateActivationRule;
    private final BlockSoundGroup soundGroup;
    private final SoundEvent doorCloseSound;
    private final SoundEvent doorOpenSound;
    private final SoundEvent trapdoorCloseSound;
    private final SoundEvent trapdoorOpenSound;
    private final SoundEvent pressurePlateClickOffSound;
    private final SoundEvent pressurePlateClickOnSound;
    private final SoundEvent buttonClickOffSound;
    private final SoundEvent buttonClickOnSound;

    public BlockSetType(String name, boolean openableByHand, boolean openableByWindCharge,
            boolean buttonActivatedByArrows, ActivationRule pressurePlateActivationRule,
            BlockSoundGroup soundGroup, SoundEvent doorCloseSound, SoundEvent doorOpenSound,
            SoundEvent trapdoorCloseSound, SoundEvent trapdoorOpenSound,
            SoundEvent pressurePlateClickOffSound, SoundEvent pressurePlateClickOnSound,
            SoundEvent buttonClickOffSound, SoundEvent buttonClickOnSound) {
        this.name = name == null ? "custom" : name;
        this.openableByHand = openableByHand;
        this.openableByWindCharge = openableByWindCharge;
        this.buttonActivatedByArrows = buttonActivatedByArrows;
        this.pressurePlateActivationRule = pressurePlateActivationRule == null ? ActivationRule.EVERYTHING : pressurePlateActivationRule;
        this.soundGroup = soundGroup == null ? BlockSoundGroup.STONE : soundGroup;
        this.doorCloseSound = doorCloseSound;
        this.doorOpenSound = doorOpenSound;
        this.trapdoorCloseSound = trapdoorCloseSound;
        this.trapdoorOpenSound = trapdoorOpenSound;
        this.pressurePlateClickOffSound = pressurePlateClickOffSound;
        this.pressurePlateClickOnSound = pressurePlateClickOnSound;
        this.buttonClickOffSound = buttonClickOffSound;
        this.buttonClickOnSound = buttonClickOnSound;
    }

    public String name() { return name; }
    public boolean openableByHand() { return openableByHand; }
    public boolean openableByWindCharge() { return openableByWindCharge; }
    public boolean buttonActivatedByArrows() { return buttonActivatedByArrows; }
    public ActivationRule pressurePlateActivationRule() { return pressurePlateActivationRule; }
    public BlockSoundGroup soundGroup() { return soundGroup; }
    public SoundEvent doorCloseSound() { return doorCloseSound; }
    public SoundEvent doorOpenSound() { return doorOpenSound; }
    public SoundEvent trapdoorCloseSound() { return trapdoorCloseSound; }
    public SoundEvent trapdoorOpenSound() { return trapdoorOpenSound; }
    public SoundEvent pressurePlateClickOffSound() { return pressurePlateClickOffSound; }
    public SoundEvent pressurePlateClickOnSound() { return pressurePlateClickOnSound; }
    public SoundEvent buttonClickOffSound() { return buttonClickOffSound; }
    public SoundEvent buttonClickOnSound() { return buttonClickOnSound; }
    public static BlockSetType register(BlockSetType type) { return type; }
    public static BlockSetType register(Identifier id, BlockSetType type) { if (id != null && type != null) REGISTERED.putIfAbsent(id, type); return type; }
    public static BlockSetType register(String id, BlockSetType type) { return register(Identifier.tryParse(id), type); }
}
