package net.fabricmc.fabric.api.entity.event.v1;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.entity.conversion.EntityConversionContext;

/** Damage/death callbacks. AllowDamage is evaluated before native damage is applied. */
public final class ServerLivingEntityEvents {
    private ServerLivingEntityEvents() { }

    @FunctionalInterface public interface AllowDamage {
        boolean allowDamage(LivingEntity entity, DamageSource source, float amount);
    }
    @FunctionalInterface public interface AfterDamage {
        /** The five-argument Fabric 1.21.4 callback shape. */
        void afterDamage(LivingEntity entity, DamageSource source,
                         float baseDamageTaken, float damageTaken, boolean blocked);

        /** Compatibility overload used by the native post-damage boundary. */
        default void afterDamage(LivingEntity entity, DamageSource source, float amount) {
            afterDamage(entity, source, amount, amount, false);
        }
    }
    @FunctionalInterface public interface AllowDeath {
        boolean allowDeath(LivingEntity entity, DamageSource source, float amount);
    }
    @FunctionalInterface public interface AfterDeath {
        void afterDeath(LivingEntity entity, DamageSource source);
    }
    @FunctionalInterface public interface MobConversion {
        void onConversion(MobEntity previous, MobEntity converted,
                          EntityConversionContext conversionContext);
    }

    public static final Event<AllowDamage> ALLOW_DAMAGE = new Event<>(CppModRuntime::registerAllowDamage,
        AllowDamage.class, callbacks -> (entity, source, amount) -> {
            for (AllowDamage callback : callbacks) {
                if (!callback.allowDamage(entity, source, amount)) return false;
            }
            return true;
        });
    public static final Event<AfterDamage> AFTER_DAMAGE = new Event<>(CppModRuntime::registerAfterDamage,
        AfterDamage.class, callbacks -> (entity, source, baseDamageTaken, damageTaken, blocked) -> {
            for (AfterDamage callback : callbacks)
                callback.afterDamage(entity, source, baseDamageTaken, damageTaken, blocked);
        });
    public static final Event<AfterDeath> AFTER_DEATH = new Event<>(CppModRuntime::registerAfterDeath,
        AfterDeath.class, callbacks -> (entity, source) -> {
            for (AfterDeath callback : callbacks) callback.afterDeath(entity, source);
        });
    public static final Event<AllowDeath> ALLOW_DEATH = EventFactory.createArrayBacked(
        AllowDeath.class, callbacks -> (entity, source, amount) -> {
            for (AllowDeath callback : callbacks)
                if (!callback.allowDeath(entity, source, amount)) return false;
            return true;
        });
    public static final Event<MobConversion> MOB_CONVERSION = EventFactory.createArrayBacked(
        MobConversion.class, callbacks -> (previous, converted, context) -> {
            for (MobConversion callback : callbacks)
                callback.onConversion(previous, converted, context);
        });

    public static void clear() {
        ALLOW_DAMAGE.clear(); AFTER_DAMAGE.clear(); AFTER_DEATH.clear(); ALLOW_DEATH.clear(); MOB_CONVERSION.clear();
    }
}
