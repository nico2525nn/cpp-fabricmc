package net.minecraft.server.world;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.entity.Entity;
import net.minecraft.world.entity.EntityLike;
import net.minecraft.world.entity.SectionedEntityCache;

/** Small entity-manager shell for Accessor and lifecycle mixins. */
public class ServerEntityManager {
    private final List<Entity> entities = new ArrayList<>();
    private final SectionedEntityCache cache = new SectionedEntityCache();
    public boolean addEntity(Entity entity, boolean temporary) {
        return entity != null && !entities.contains(entity) && entities.add(entity);
    }
    public boolean addEntity(EntityLike entity, boolean temporary) {
        return entity instanceof Entity concrete && addEntity(concrete, temporary);
    }
    public void remove(Entity entity) { entities.remove(entity); }
    public List<Entity> getEntities() { return List.copyOf(entities); }
    public void tick() { }
}
