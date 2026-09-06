package net.minecraft.block;

/** Base ABI for blocks whose behavior is backed by a block entity. */
public abstract class BlockWithEntity extends Block {
    protected BlockWithEntity(AbstractBlock.Settings settings) { super(settings); }
    protected BlockWithEntity() { super(AbstractBlock.Settings.create()); }
}
