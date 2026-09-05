package net.minecraft.block;

import net.minecraft.util.Identifier;
import java.util.concurrent.atomic.AtomicInteger;

public class Block {
    private static final AtomicInteger NEXT_CUSTOM_STATE = new AtomicInteger(10000);
    private final int rawState;
    private final Identifier id;
    public Block(int rawState) { this(rawState, rawState == 0 ? Identifier.of("minecraft", "air") : Identifier.of("minecraft", "state_" + rawState)); }
    public Block(int rawState, Identifier id) { this.rawState = rawState; this.id = id; }
    public Block(AbstractBlock.Settings settings) {
        int state = NEXT_CUSTOM_STATE.getAndIncrement();
        this.rawState = state;
        this.id = Identifier.of("cppfm", "custom_block_" + state);
    }
    public int getRawState() { return rawState; }
    public Identifier getId() { return id; }
    public BlockState getDefaultState() { return new BlockState(rawState, this); }
    public static BlockState getBlockFromItem(net.minecraft.item.Item item) { return item == null ? Blocks.AIR.getDefaultState() : new BlockState(item.getRawState()); }
}
