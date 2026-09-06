package net.minecraft.block;

import java.util.concurrent.atomic.AtomicInteger;
import net.minecraft.item.Item;
import net.minecraft.registry.RegistryEntry;
import net.minecraft.state.StateManager;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.shape.VoxelShape;

public class Block extends AbstractBlock implements net.minecraft.item.ItemConvertible {
    private static final AtomicInteger NEXT_CUSTOM_STATE = new AtomicInteger(10000);
    private final int rawState;
    private final Identifier id;
    private final StateManager<Block, BlockState> stateManager;
    private net.minecraft.item.BlockItem item;
    public Block(int rawState) { this(rawState, rawState == 0 ? Identifier.of("minecraft", "air") : Identifier.of("minecraft", "state_" + rawState), AbstractBlock.Settings.create()); }
    public Block(int rawState, Identifier id) { this(rawState, id, AbstractBlock.Settings.create()); }
    private Block(int rawState, Identifier id, AbstractBlock.Settings settings) {
        super(settings);
        this.rawState = Math.max(0, rawState); this.id = id == null ? Identifier.of("minecraft", "air") : id;
        this.stateManager = new StateManager<>(this, this::appendProperties);
    }
    public Block(AbstractBlock.Settings settings) {
        super(settings);
        int state = NEXT_CUSTOM_STATE.getAndIncrement();
        this.rawState = state;
        this.id = Identifier.of("cppfm", "custom_block_" + state);
        this.stateManager = new StateManager<>(this, this::appendProperties);
    }
    public int getRawState() { return rawState; }
    public Identifier getId() { return id; }
    public BlockState getDefaultState() { return new BlockState(rawState, this); }
    public StateManager<Block, BlockState> getStateManager() { return stateManager; }
    protected void appendProperties(StateManager.Builder<Block, BlockState> builder) { }
    public BlockState getStateWithProperties(BlockState state) { return state == null ? getDefaultState() : state; }
    @Override public Item asItem() { if (item == null) item = new net.minecraft.item.BlockItem(this); return item; }
    public String getTranslationKey() { return "block." + id.getNamespace() + "." + id.getPath().replace('/', '.'); }
    public RegistryEntry<Block> getRegistryEntry() { return net.minecraft.registry.Registries.BLOCK.getEntry(this).orElse(null); }
    public net.minecraft.registry.entry.RegistryEntry<Block> getCanonicalRegistryEntry() {
        return getRegistryEntry();
    }
    public float getHardness() { return settings.hardness(); }
    public float getSlipperiness() { return settings.slipperiness(); }
    public float getVelocityMultiplier() { return settings.velocityMultiplier(); }
    public int getLuminance() { return settings.luminance(); }
    public boolean isOpaque() { return settings.opaqueValue(); }
    public boolean emitsRedstonePower(BlockState state) { return false; }
    public static boolean isShapeFullCube(VoxelShape shape) {
        return shape != null && shape.isCube();
    }
    public BlockState getPlacementState(net.minecraft.item.ItemPlacementContext context) { return getDefaultState(); }
    public void onPlaced(net.minecraft.world.World world, BlockPos pos, BlockState state,
                         net.minecraft.entity.LivingEntity placer, net.minecraft.item.ItemStack itemStack) { }
    /** Canonical 1.21.4 name for the post-placement callback. */
    public void setPlacedBy(net.minecraft.world.World world, BlockPos pos, BlockState state,
                            net.minecraft.entity.LivingEntity placer, net.minecraft.item.ItemStack itemStack) {
        onPlaced(world, pos, state, placer, itemStack);
    }
    public void onBreak(net.minecraft.world.World world, BlockPos pos, BlockState state) { }
    public BlockState onUse(net.minecraft.world.World world, BlockPos pos, BlockState state, net.minecraft.entity.player.PlayerEntity player) { return state; }
    public static Block getBlockFromItem(Item item) { return item instanceof net.minecraft.item.BlockItem blockItem ? blockItem.getBlock() : Blocks.AIR; }
    public static BlockState getBlockStateFromItem(Item item) { return getBlockFromItem(item).getDefaultState(); }
}
