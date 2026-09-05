package net.minecraft.entity;

import java.util.Objects;
import java.util.function.BiFunction;
import java.util.function.Supplier;
import net.minecraft.registry.RegistryEntry;
import net.minecraft.util.Identifier;
import net.minecraft.world.World;

/** Lightweight entity type descriptor; native entities remain authoritative. */
public class EntityType<T extends Entity> {
    public static final EntityType<Entity> UNKNOWN = new EntityType<>(Identifier.of("minecraft", "unknown"), () -> null, 0.6f, 1.8f);
    public static final EntityType<Entity> PLAYER = new EntityType<>(Identifier.of("minecraft", "player"), () -> null, 0.6f, 1.8f);
    private final Identifier id;
    private final Supplier<? extends T> factory;
    private final BiFunction<EntityType<T>, World, ? extends T> worldFactory;
    private final float width;
    private final float height;

    public EntityType(Identifier id, Supplier<? extends T> factory, float width, float height) {
        this.id = id == null ? Identifier.of("minecraft", "unknown") : id;
        this.factory = factory == null ? () -> null : factory;
        this.worldFactory = null;
        this.width = width; this.height = height;
    }
    public EntityType(Identifier id, BiFunction<EntityType<T>, World, ? extends T> factory, float width, float height) {
        this.id = id == null ? Identifier.of("minecraft", "unknown") : id;
        this.factory = null;
        this.worldFactory = factory;
        this.width = width; this.height = height;
    }
    public Identifier getId() { return id; }
    public static EntityType<?> byId(String id) {
        Identifier key = Identifier.tryParse(id);
        if (key == null) return UNKNOWN;
        EntityType<?> value = net.minecraft.registry.Registries.ENTITY_TYPE.get(key);
        return value == null ? UNKNOWN : value;
    }
    public static EntityType<?> get(String id) { return byId(id); }
    public float getWidth() { return width; }
    public float getHeight() { return height; }
    public T create(World world) {
        T entity = worldFactory == null ? factory.get() : worldFactory.apply(this, world);
        if (entity != null && world != null) entity.setWorld(world);
        return entity;
    }
    public String getTranslationKey() { return "entity." + id.getNamespace() + "." + id.getPath().replace('/', '.'); }
    public RegistryEntry<EntityType<T>> getRegistryEntry() {
        @SuppressWarnings("unchecked") RegistryEntry<EntityType<T>> entry = (RegistryEntry<EntityType<T>>) (RegistryEntry<?>)
            net.minecraft.registry.Registries.ENTITY_TYPE.getEntry((EntityType<?>) this).orElse(null);
        return entry;
    }
    @Override public boolean equals(Object other) { return other instanceof EntityType<?> t && id.equals(t.id); }
    @Override public int hashCode() { return Objects.hash(id); }
    @Override public String toString() { return id.toString(); }
}
