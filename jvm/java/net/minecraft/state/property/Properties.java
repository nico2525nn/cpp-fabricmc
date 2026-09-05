package net.minecraft.state.property;

public final class Properties {
    private Properties() {}
    public static final BooleanProperty POWERED = BooleanProperty.of("powered");
    public static final BooleanProperty OPEN = BooleanProperty.of("open");
    public static final BooleanProperty WATERLOGGED = BooleanProperty.of("waterlogged");
    public static final BooleanProperty LIT = BooleanProperty.of("lit");
    public static final BooleanProperty ATTACHED = BooleanProperty.of("attached");
    public static final IntProperty AGE_15 = IntProperty.of("age", 0, 15);
    public static final IntProperty AGE_7 = IntProperty.of("age", 0, 7);
    public static final DirectionProperty HORIZONTAL_FACING = DirectionProperty.of("facing", direction -> direction.getAxis().isHorizontal());
}
