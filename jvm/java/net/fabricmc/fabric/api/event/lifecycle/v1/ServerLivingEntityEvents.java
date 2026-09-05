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
    @FunctionalInterface public interface AfterDamage { void afterDamage(LivingEntity entity, DamageSource source, float amount); }
    public static final Event<AllowDamage> ALLOW_DAMAGE = new Event<>(CppModRuntime::registerLegacyAllowDamage, AllowDamage.class, callbacks -> (entity, source, amount) -> { for (AllowDamage callback : callbacks) if (!callback.allowDamage(entity, source, amount)) return false; return true; });
    public static final Event<AfterDeath> AFTER_DEATH = new Event<>(CppModRuntime::registerLegacyAfterDeath, AfterDeath.class, callbacks -> (entity, source) -> { for (AfterDeath callback : callbacks) callback.afterDeath(entity, source); });
    public static final Event<AfterDamage> AFTER_DAMAGE = new Event<>(CppModRuntime::registerLegacyAfterDamage, AfterDamage.class, callbacks -> (entity, source, amount) -> { for (AfterDamage callback : callbacks) callback.afterDamage(entity, source, amount); });
    public static void clear() { ALLOW_DAMAGE.clear(); AFTER_DEATH.clear(); AFTER_DAMAGE.clear(); }
}
