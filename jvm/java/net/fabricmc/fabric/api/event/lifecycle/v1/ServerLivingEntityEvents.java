package net.fabricmc.fabric.api.event.lifecycle.v1;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.entity.DamageSource;
import net.minecraft.entity.LivingEntity;

public final class ServerLivingEntityEvents {
    private ServerLivingEntityEvents() {}
    @FunctionalInterface public interface AllowDamage { boolean allowDamage(LivingEntity entity, DamageSource source, float amount); }
    @FunctionalInterface public interface AfterDeath { void afterDeath(LivingEntity entity, DamageSource source); }
    @FunctionalInterface public interface AfterDamage {
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
    public static final Event<AllowDamage> ALLOW_DAMAGE = new Event<>(CppModRuntime::registerLegacyAllowDamage, AllowDamage.class, callbacks -> (entity, source, amount) -> { for (AllowDamage callback : callbacks) if (!callback.allowDamage(entity, source, amount)) return false; return true; });
    public static final Event<AfterDeath> AFTER_DEATH = new Event<>(CppModRuntime::registerLegacyAfterDeath, AfterDeath.class, callbacks -> (entity, source) -> { for (AfterDeath callback : callbacks) callback.afterDeath(entity, source); });
    public static final Event<AfterDamage> AFTER_DAMAGE = new Event<>(CppModRuntime::registerLegacyAfterDamage, AfterDamage.class, callbacks -> (entity, source, baseDamageTaken, damageTaken, blocked) -> { for (AfterDamage callback : callbacks) callback.afterDamage(entity, source, baseDamageTaken, damageTaken, blocked); });
    public static final Event<AllowDeath> ALLOW_DEATH = EventFactory.createArrayBacked(
        AllowDeath.class, callbacks -> (entity, source, amount) -> {
            for (AllowDeath callback : callbacks)
                if (!callback.allowDeath(entity, source, amount)) return false;
            return true;
        });
    public static void clear() { ALLOW_DAMAGE.clear(); AFTER_DEATH.clear(); AFTER_DAMAGE.clear(); ALLOW_DEATH.clear(); }
}
