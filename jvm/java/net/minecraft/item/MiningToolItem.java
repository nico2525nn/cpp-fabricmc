package net.minecraft.item;

import net.minecraft.block.Block;
import net.minecraft.registry.tag.TagKey;

/** Common mining-tool ABI used by item and block mixins. */
public class MiningToolItem extends Item {
    protected final ToolMaterial material;
    protected final TagKey<Block> effectiveBlocks;
    protected final float attackDamage;
    protected final float attackSpeed;

    public MiningToolItem(ToolMaterial material, TagKey<Block> effectiveBlocks,
                          float attackDamage, float attackSpeed, Item.Settings settings) {
        super(settings);
        this.material = material;
        this.effectiveBlocks = effectiveBlocks;
        this.attackDamage = attackDamage;
        this.attackSpeed = attackSpeed;
    }
    public ToolMaterial getMaterial() { return material; }
    public float getAttackDamage() { return attackDamage; }
    public float getAttackSpeed() { return attackSpeed; }
}
