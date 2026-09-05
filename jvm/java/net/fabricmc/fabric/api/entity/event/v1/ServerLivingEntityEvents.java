package net.fabricmc.fabric.api.entity.event.v1;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.minecraft.entity.DamageSource;
import net.minecraft.entity.LivingEntity;

/** Damage/death callbacks. AllowDamage is evaluated before native damage is applied. */
public final class ServerLivingEntityEvents {
    private ServerLivingEntityEvents() { }

    @FunctionalInterface public interface AllowDamage {
        boolean allowDamage(LivingEntity entity, DamageSource source, float amount);
    }
    @FunctionalInterface public interface AfterDamage {
        void afterDamage(LivingEntity entity, DamageSource source, float amount);
    }
    @FunctionalInterface public interface AfterDeath {
        void afterDeath(LivingEntity entity, DamageSource source);
    }

    public static final Event<AllowDamage> ALLOW_DAMAGE = new Event<>(CppModRuntime::registerAllowDamage,
        AllowDamage.class, callbacks -> (entity, source, amount) -> {
            for (AllowDamage callback : callbacks) {
                if (!callback.allowDamage(entity, source, amount)) return false;
            }
            return true;
        });
    public static final Event<AfterDamage> AFTER_DAMAGE = new Event<>(CppModRuntime::registerAfterDamage,
        AfterDamage.class, callbacks -> (entity, source, amount) -> {
            for (AfterDamage callback : callbacks) callback.afterDamage(entity, source, amount);
        });
    public static final Event<AfterDeath> AFTER_DEATH = new Event<>(CppModRuntime::registerAfterDeath,
        AfterDeath.class, callbacks -> (entity, source) -> {
            for (AfterDeath callback : callbacks) callback.afterDeath(entity, source);
        });

    public static void clear() {
        ALLOW_DAMAGE.clear(); AFTER_DAMAGE.clear(); AFTER_DEATH.clear();
    }
}
