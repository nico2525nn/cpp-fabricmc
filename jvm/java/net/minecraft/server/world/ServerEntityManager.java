package net.minecraft.server.world;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.entity.Entity;
import net.minecraft.world.entity.EntityLike;
import net.minecraft.world.entity.EntityIndex;
import net.minecraft.world.entity.SectionedEntityCache;
import net.minecraft.world.entity.SimpleEntityLookup;

/** Small entity-manager shell for Accessor and lifecycle mixins. */
public class ServerEntityManager {
    private final List<Entity> entities = new ArrayList<>();
    private final SectionedEntityCache cache = new SectionedEntityCache();
    private final EntityIndex<Entity> index = new EntityIndex<>();
    private final SimpleEntityLookup<Entity> entityLookup = new SimpleEntityLookup<>(index, cache);
    public boolean addEntity(Entity entity, boolean temporary) {
        if (entity == null || entities.contains(entity)) return false;
        boolean added = entities.add(entity);
        if (added) index.add(entity);
        return added;
    }
    public boolean addEntity(EntityLike entity, boolean temporary) {
        return entity instanceof Entity concrete && addEntity(concrete, temporary);
    }
    public void remove(Entity entity) { if (entities.remove(entity)) index.remove(entity); }
    public List<Entity> getEntities() { return List.copyOf(entities); }
    public void tick() { }
    /** Private accessor target used by spark's server entity instrumentation. */
    private SimpleEntityLookup<Entity> getEntityLookup() { return entityLookup; }
}
