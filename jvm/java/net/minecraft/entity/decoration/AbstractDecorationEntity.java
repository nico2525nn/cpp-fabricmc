package net.minecraft.entity.decoration;

import java.util.function.Predicate;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityType;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.util.math.Direction;
import net.minecraft.world.World;

/** Shared ABI for wall-mounted decoration entities in 1.21.4. */
public abstract class AbstractDecorationEntity extends Entity {
    protected static final Predicate<Entity> PREDICATE = Entity::isAlive;
    protected Direction facing = Direction.SOUTH;

    protected AbstractDecorationEntity(EntityType<?> type, World world,
                                       BlockPos pos, Direction facing) {
        super(type, world);
        if (pos != null) setPosition(pos.getX() + 0.5, pos.getY(), pos.getZ() + 0.5);
        if (facing != null) this.facing = facing;
    }

    protected AbstractDecorationEntity() { super((EntityType<?>) null, (World) null); }

    protected boolean canStayAttached(Entity entity) { return entity != null && entity.isAlive(); }

    protected Box calculateBoundingBox(BlockPos pos, Direction side) {
        if (pos == null) pos = BlockPos.ORIGIN;
        if (side == null) side = Direction.SOUTH;
        double x = pos.getX() + 0.5;
        double y = pos.getY() + 0.5;
        double z = pos.getZ() + 0.5;
        double offset = 0.46875;
        x += side.getOffsetX() * offset;
        y += side.getOffsetY() * offset;
        z += side.getOffsetZ() * offset;
        return new Box(x - 0.5, y - 0.5, z - 0.5,
                       x + 0.5, y + 0.5, z + 0.5);
    }

    protected boolean canStayAttached(BlockPos pos) { return pos != null; }
    protected boolean canStayAttached() { return canStayAttached(getBlockPos()); }

    public Box getAttachmentBox() { return getBoundingBox(); }

    public void setFacing(Direction value) { if (value != null) facing = value; }

    public Direction getFacing() { return facing; }

    public void onPlace() { }
}
