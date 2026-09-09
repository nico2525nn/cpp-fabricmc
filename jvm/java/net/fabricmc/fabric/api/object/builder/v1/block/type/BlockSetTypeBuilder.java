package net.fabricmc.fabric.api.object.builder.v1.block.type;

import net.minecraft.block.BlockSetType;
import net.minecraft.sound.BlockSoundGroup;
import net.minecraft.sound.SoundEvent;
import net.minecraft.util.Identifier;

/** Builder matching the 1.21.4 door/button/pressure-plate type policy. */
public final class BlockSetTypeBuilder {
    private boolean openableByHand = true;
    private boolean openableByWindCharge = true;
    private boolean buttonActivatedByArrows = true;
    private BlockSetType.ActivationRule activationRule = BlockSetType.ActivationRule.EVERYTHING;
    private BlockSoundGroup soundGroup = BlockSoundGroup.STONE;
    private SoundEvent doorCloseSound, doorOpenSound, trapdoorCloseSound, trapdoorOpenSound;
    private SoundEvent pressurePlateClickOffSound, pressurePlateClickOnSound, buttonClickOffSound, buttonClickOnSound;
    public BlockSetTypeBuilder() { }
    public BlockSetTypeBuilder openableByHand(boolean value) { openableByHand = value; return this; }
    public BlockSetTypeBuilder openableByWindCharge(boolean value) { openableByWindCharge = value; return this; }
    public BlockSetTypeBuilder buttonActivatedByArrows(boolean value) { buttonActivatedByArrows = value; return this; }
    public BlockSetTypeBuilder pressurePlateActivationRule(BlockSetType.ActivationRule value) { activationRule = value; return this; }
    public BlockSetTypeBuilder soundGroup(BlockSoundGroup value) { if (value != null) soundGroup = value; return this; }
    public BlockSetTypeBuilder doorCloseSound(SoundEvent value) { doorCloseSound = value; return this; }
    public BlockSetTypeBuilder doorOpenSound(SoundEvent value) { doorOpenSound = value; return this; }
    public BlockSetTypeBuilder trapdoorCloseSound(SoundEvent value) { trapdoorCloseSound = value; return this; }
    public BlockSetTypeBuilder trapdoorOpenSound(SoundEvent value) { trapdoorOpenSound = value; return this; }
    public BlockSetTypeBuilder pressurePlateClickOffSound(SoundEvent value) { pressurePlateClickOffSound = value; return this; }
    public BlockSetTypeBuilder pressurePlateClickOnSound(SoundEvent value) { pressurePlateClickOnSound = value; return this; }
    public BlockSetTypeBuilder buttonClickOffSound(SoundEvent value) { buttonClickOffSound = value; return this; }
    public BlockSetTypeBuilder buttonClickOnSound(SoundEvent value) { buttonClickOnSound = value; return this; }
    public static BlockSetTypeBuilder copyOf(BlockSetTypeBuilder value) { return value == null ? new BlockSetTypeBuilder() : value; }
    public static BlockSetTypeBuilder copyOf(BlockSetType value) {
        return new BlockSetTypeBuilder().openableByHand(value.openableByHand()).openableByWindCharge(value.openableByWindCharge())
            .buttonActivatedByArrows(value.buttonActivatedByArrows()).pressurePlateActivationRule(value.pressurePlateActivationRule())
            .soundGroup(value.soundGroup()).doorCloseSound(value.doorCloseSound()).doorOpenSound(value.doorOpenSound())
            .trapdoorCloseSound(value.trapdoorCloseSound()).trapdoorOpenSound(value.trapdoorOpenSound())
            .pressurePlateClickOffSound(value.pressurePlateClickOffSound()).pressurePlateClickOnSound(value.pressurePlateClickOnSound())
            .buttonClickOffSound(value.buttonClickOffSound()).buttonClickOnSound(value.buttonClickOnSound());
    }
    public BlockSetType register(Identifier id) { return BlockSetType.register(id, build(id)); }
    public BlockSetType build(Identifier id) {
        return new BlockSetType(id == null ? "custom" : id.toString(), openableByHand, openableByWindCharge,
            buttonActivatedByArrows, activationRule, soundGroup, doorCloseSound, doorOpenSound,
            trapdoorCloseSound, trapdoorOpenSound, pressurePlateClickOffSound, pressurePlateClickOnSound,
            buttonClickOffSound, buttonClickOnSound);
    }
}
