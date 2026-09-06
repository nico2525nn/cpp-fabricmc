package net.minecraft.world.block;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.random.Random;

/**
 * Compact orientation value used by the 1.21.4 redstone evaluator.
 *
 * <p>Only the stable value semantics are represented here; redstone
 * propagation remains native.  Keeping the value object real is important
 * because Lithium and Carpet pass it through transformed method descriptors.</p>
 */
public final class WireOrientation {
    public static final PacketCodec<Object, WireOrientation> PACKET_CODEC =
        PacketCodec.unit(new WireOrientation(Direction.UP, Direction.NORTH, SideBias.LEFT));

    private static final WireOrientation[] VALUES = new WireOrientation[] {
        new WireOrientation(Direction.UP, Direction.NORTH, SideBias.LEFT),
        new WireOrientation(Direction.UP, Direction.NORTH, SideBias.RIGHT),
        new WireOrientation(Direction.UP, Direction.SOUTH, SideBias.LEFT),
        new WireOrientation(Direction.UP, Direction.SOUTH, SideBias.RIGHT),
        new WireOrientation(Direction.UP, Direction.WEST, SideBias.LEFT),
        new WireOrientation(Direction.UP, Direction.WEST, SideBias.RIGHT),
        new WireOrientation(Direction.UP, Direction.EAST, SideBias.LEFT),
        new WireOrientation(Direction.UP, Direction.EAST, SideBias.RIGHT)
    };

    private final Direction up;
    private final Direction front;
    private final Direction right;
    private final SideBias sideBias;
    private final int ordinal;

    public WireOrientation(Direction up, Direction front, SideBias sideBias) {
        this.up = up == null ? Direction.UP : up;
        this.front = front == null ? Direction.NORTH : front;
        this.sideBias = sideBias == null ? SideBias.LEFT : sideBias;
        this.right = this.sideBias == SideBias.LEFT
            ? this.front.rotateYCounterclockwise() : this.front.rotateYClockwise();
        this.ordinal = ordinalFromComponents(this.up, this.front, this.sideBias);
    }

    public static WireOrientation of(Direction up, Direction front, SideBias sideBias) {
        return new WireOrientation(up, front, sideBias);
    }

    public static WireOrientation fromOrdinal(int ordinal) {
        if (ordinal >= 0 && ordinal < VALUES.length) return VALUES[ordinal];
        return VALUES[0];
    }

    public static WireOrientation random(Random random) {
        return VALUES[random == null ? 0 : random.nextInt(VALUES.length)];
    }

    public static WireOrientation initializeValuesArray(WireOrientation prime, WireOrientation[] valuesOut) {
        if (valuesOut != null) System.arraycopy(VALUES, 0, valuesOut, 0, Math.min(valuesOut.length, VALUES.length));
        return prime;
    }

    public static int ordinalFromComponents(Direction up, Direction front, SideBias sideBias) {
        int base = Math.max(0, (up == null ? Direction.UP : up).getId()) * 2;
        int facing = Math.max(0, (front == null ? Direction.NORTH : front).getId());
        int bias = sideBias == SideBias.RIGHT ? 1 : 0;
        return Math.floorMod(base + facing + bias, VALUES.length);
    }

    public WireOrientation withUp(Direction direction) {
        return new WireOrientation(direction, front, sideBias);
    }

    public WireOrientation withFront(Direction direction) {
        return new WireOrientation(up, direction, sideBias);
    }

    public WireOrientation withFrontIfNotUp(Direction direction) {
        return direction == null || direction == up ? this : withFront(direction);
    }

    public WireOrientation withSideBias(SideBias bias) {
        return new WireOrientation(up, front, bias);
    }

    public WireOrientation withFrontAndSideBias(Direction direction) {
        return new WireOrientation(up, direction, sideBias);
    }

    public WireOrientation withOppositeSideBias() {
        return withSideBias(sideBias.opposite());
    }

    public Direction getUp() { return up; }
    public Direction getFront() { return front; }
    public Direction getRight() { return right; }
    public SideBias getSideBias() { return sideBias; }
    public int ordinal() { return ordinal; }

    public List<Direction> getVerticalDirections() {
        return Collections.unmodifiableList(Arrays.asList(up, up.getOpposite()));
    }

    public List<Direction> getHorizontalDirections() {
        return Collections.unmodifiableList(Arrays.asList(front, right, front.getOpposite(), right.getOpposite()));
    }

    public List<Direction> getDirectionsByPriority() {
        return getVerticalDirections();
    }

    public boolean method_61855(Direction direction) { return direction == front || direction == right; }
    public boolean method_61857(Direction direction) { return direction == up || direction == up.getOpposite(); }

    public enum SideBias {
        LEFT("left"), RIGHT("right");

        private final String name;
        SideBias(String name) { this.name = name; }
        public SideBias opposite() { return this == LEFT ? RIGHT : LEFT; }
        public String asString() { return name; }
    }
}
