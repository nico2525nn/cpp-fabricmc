package net.minecraft.command;

public enum EntityAnchor {
    FEET, EYES;
    public net.minecraft.util.math.Vec3d positionAt(net.minecraft.entity.Entity entity) {
        if (entity == null) return net.minecraft.util.math.Vec3d.ZERO;
        return this == EYES ? entity.getEyePos() : entity.getPos();
    }
}
