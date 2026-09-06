package net.minecraft.block.entity;

import net.minecraft.inventory.Inventory;
import net.minecraft.util.math.Box;

/**
 * The 1.21.4 hopper ABI shared by HopperBlockEntity and hopper optimizers.
 *
 * <p>The native block-entity implementation owns item transfer.  This
 * interface keeps the vanilla geometry and coordinate contract available to
 * Fabric mixins and accessors.</p>
 */
public interface Hopper extends Inventory {
    Box INPUT_AREA_SHAPE = new Box(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);

    double getHopperX();

    double getHopperY();

    double getHopperZ();

    boolean canBlockFromAbove();

    Box getInputAreaShape();
}
