package net.minecraft.network.packet.c2s.play;

import net.minecraft.network.packet.Packet;
import net.minecraft.util.Hand;
import net.minecraft.util.math.Vec3d;

/** Serverbound entity-interaction packet ABI used by Fabric networking mixins. */
public class PlayerInteractEntityC2SPacket implements Packet<Object> {
    private final int entityId;
    private final boolean playerSneaking;

    public PlayerInteractEntityC2SPacket() { this(0, false); }
    public PlayerInteractEntityC2SPacket(int entityId, boolean playerSneaking) {
        this.entityId = entityId;
        this.playerSneaking = playerSneaking;
    }

    public int getEntityId() { return entityId; }
    public boolean isPlayerSneaking() { return playerSneaking; }
    public void handle(Handler handler) { }
    public void apply(Object listener) { }

    public interface Handler {
        default void interactAt(Hand hand, Vec3d pos) { }
        default void attack() { }
        default void interact(Hand hand) { }
    }

    public abstract static class InteractTypeHandler {
        public abstract InteractType getType();
        public void write(Object buf) { }
        public void handle(Handler handler) { }
    }

    public static final class InteractType {
        public static final InteractType ATTACK = new InteractType();
        public static final InteractType INTERACT_AT = new InteractType();
        public static final InteractType INTERACT = new InteractType();
        private InteractType() { }
    }

    public static final class InteractHandler extends InteractTypeHandler {
        private final Hand hand;
        public InteractHandler(Hand hand) { this.hand = hand; }
        public Hand getHand() { return hand; }
        @Override public InteractType getType() { return InteractType.INTERACT; }
    }

    public static final class InteractAtHandler extends InteractTypeHandler {
        private final Hand hand;
        private final Vec3d pos;
        public InteractAtHandler(Hand hand, Vec3d pos) { this.hand = hand; this.pos = pos; }
        public Hand getHand() { return hand; }
        public Vec3d getPos() { return pos; }
        @Override public InteractType getType() { return InteractType.INTERACT_AT; }
    }
}
