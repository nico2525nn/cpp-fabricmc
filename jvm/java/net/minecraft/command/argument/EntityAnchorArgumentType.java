package net.minecraft.command.argument;

import net.minecraft.entity.Entity;
import net.minecraft.util.math.Vec3d;

/** Bounded canonical entity-anchor argument surface used by command mods. */
public class EntityAnchorArgumentType {
    private final EntityAnchor entityAnchor;

    public EntityAnchorArgumentType() { this(EntityAnchor.FEET); }
    public EntityAnchorArgumentType(EntityAnchor entityAnchor) {
        this.entityAnchor = entityAnchor == null ? EntityAnchor.FEET : entityAnchor;
    }
    public EntityAnchor getEntityAnchor() { return entityAnchor; }

    public static final class EntityAnchor {
        public static final EntityAnchor EYES = new EntityAnchor("eyes", true);
        public static final EntityAnchor FEET = new EntityAnchor("feet", false);
        private final String id;
        private final boolean eyes;

        private EntityAnchor(String id, boolean eyes) { this.id = id; this.eyes = eyes; }
        public Vec3d positionAt(Entity entity) {
            return entity == null ? Vec3d.ZERO
                : eyes ? entity.getEyePos() : new Vec3d(entity.getX(), entity.getY(), entity.getZ());
        }
        public static EntityAnchor fromId(String id) {
            if ("eyes".equals(id)) return EYES;
            if ("feet".equals(id)) return FEET;
            return null;
        }
        public String id() { return id; }
        @Override public String toString() { return id; }
    }
}
