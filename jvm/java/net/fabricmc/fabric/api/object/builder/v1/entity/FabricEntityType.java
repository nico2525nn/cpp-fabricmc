package net.fabricmc.fabric.api.object.builder.v1.entity;

import java.util.function.Supplier;
import java.util.function.UnaryOperator;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.SpawnGroup;
import net.minecraft.entity.SpawnLocation;
import net.minecraft.entity.SpawnRestriction;
import net.minecraft.entity.attribute.DefaultAttributeContainer;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.world.Heightmap;

/** Access-widened entity builder settings used by Fabric object builders. */
public interface FabricEntityType {
    interface Builder<T extends Entity> {
        default EntityType.Builder<T> alwaysUpdateVelocity(boolean value) {
            if (this instanceof FabricEntityTypeBuilder<?>) {
                FabricEntityTypeBuilder<T> builder = (FabricEntityTypeBuilder<T>) this;
                builder.alwaysUpdateVelocitySetting(value);
                return builder.vanillaBuilder();
            }
            return (EntityType.Builder<T>) this;
        }
        default EntityType.Builder<T> canPotentiallyExecuteCommands(boolean value) {
            if (this instanceof FabricEntityTypeBuilder<?>) {
                FabricEntityTypeBuilder<T> builder = (FabricEntityTypeBuilder<T>) this;
                builder.canPotentiallyExecuteCommandsSetting(value);
                return builder.vanillaBuilder();
            }
            return (EntityType.Builder<T>) this;
        }
        static <T extends LivingEntity> EntityType.Builder<T> createLiving(EntityType.EntityFactory<T> factory,
                SpawnGroup group, UnaryOperator<Living<T>> operator) {
            FabricEntityTypeBuilder.Living<T> builder = FabricEntityTypeBuilder.createLiving();
            builder.spawnGroup(group).entityFactory(factory);
            if (operator != null) operator.apply(builder);
            return builder.vanillaBuilder();
        }
        static <T extends MobEntity> EntityType.Builder<T> createMob(EntityType.EntityFactory<T> factory,
                SpawnGroup group, UnaryOperator<Mob<T>> operator) {
            FabricEntityTypeBuilder.Mob<T> builder = FabricEntityTypeBuilder.createMob();
            builder.spawnGroup(group).entityFactory(factory);
            if (operator != null) operator.apply(builder);
            return builder.vanillaBuilder();
        }
        interface Living<T extends LivingEntity> {
            Living<T> defaultAttributes(Supplier<DefaultAttributeContainer.Builder> attributes);
        }
        interface Mob<T extends MobEntity> extends Living<T> {
            Mob<T> spawnRestriction(SpawnLocation location, Heightmap.Type heightmap,
                    SpawnRestriction.SpawnPredicate<T> predicate);
            Mob<T> defaultAttributes(Supplier<DefaultAttributeContainer.Builder> attributes);
        }
    }
}
