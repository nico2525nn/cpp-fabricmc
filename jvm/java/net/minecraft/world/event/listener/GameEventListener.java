package net.minecraft.world.event.listener;

import net.minecraft.util.math.Vec3d;

/** Linkable game-event listener marker used by the dispatcher ABI. */
public interface GameEventListener {
    default Vec3d getPos() { return Vec3d.ZERO; }
}
