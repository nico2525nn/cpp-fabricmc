package cppfm.api_fixture;

import cppfm.bridge.CppModRuntime;
import java.lang.reflect.Method;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents;
import net.fabricmc.fabric.api.entity.event.v1.ServerPlayerEvents;
import net.minecraft.entity.LivingEntity;
import net.minecraft.server.network.ServerPlayerEntity;

/** Focused fixture for native death-gate and post-damage callback dispatch. */
@SuppressWarnings("deprecation")
public final class DamageEventsContractTest {
    private static int checks;

    private record DamageObservation(LivingEntity victim, String cause, float base,
                                    float taken, boolean blocked) { }

    private static final class LocalLivingEntityProbe extends LivingEntity {
        LocalLivingEntityProbe() {
            super(net.minecraft.entity.EntityType.UNKNOWN, null);
        }
    }

    private DamageEventsContractTest() { }

    public static void main(String[] args) throws Exception {
        if (!CppModRuntime.bootstrap("/tmp/cppfm-jvm-damage-fixture-no-mods",
                                     "/tmp/cppfm-jvm-damage-fixture-config"))
            throw new AssertionError("runtime bootstrap");
        try {
            int passed = run();
            System.out.println("JVM damage event fixture: " + passed + " PASS");
        } finally {
            CppModRuntime.shutdown();
        }
    }

    public static int run() throws Exception {
        checks = 0;
        AtomicInteger modernAllow = new AtomicInteger();
        AtomicInteger legacyAllow = new AtomicInteger();
        AtomicInteger playerAllow = new AtomicInteger();
        AtomicInteger allowAfterException = new AtomicInteger();
        AtomicReference<LivingEntity> allowVictim = new AtomicReference<>();

        ServerLivingEntityEvents.ALLOW_DEATH.register((victim, source, amount) -> {
            modernAllow.incrementAndGet();
            allowVictim.set(victim);
            return !"modern-veto".equals(source.getName());
        });
        ServerLivingEntityEvents.ALLOW_DEATH.register((victim, source, amount) -> {
            if ("listener-exception".equals(source.getName()))
                throw new IllegalStateException("fixture allow-death failure");
            return true;
        });
        ServerLivingEntityEvents.ALLOW_DEATH.register((victim, source, amount) -> {
            if ("listener-exception".equals(source.getName()))
                allowAfterException.incrementAndGet();
            return true;
        });
        net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.ALLOW_DEATH
            .register((victim, source, amount) -> {
                legacyAllow.incrementAndGet();
                return !"legacy-veto".equals(source.getName());
            });
        ServerPlayerEvents.ALLOW_DEATH.register((player, source, amount) -> {
            playerAllow.incrementAndGet();
            return !"player-veto".equals(source.getName());
        });

        LivingEntity mob = new LocalLivingEntityProbe();
        check(CppModRuntime.dispatchEntityAllowDeath(mob, 4.5f, "allowed"),
            "modern and legacy allow-death callbacks permit death");
        check(modernAllow.get() == 1 && legacyAllow.get() == 1 && allowVictim.get() == mob,
            "living callbacks receive the canonical victim");

        int legacyBeforeVeto = legacyAllow.get();
        check(!CppModRuntime.dispatchEntityAllowDeath(mob, 4.5f, "modern-veto"),
            "modern false vetoes native death");
        check(legacyAllow.get() == legacyBeforeVeto,
            "modern veto short-circuits later event groups");
        check(!CppModRuntime.dispatchEntityAllowDeath(mob, 4.5f, "legacy-veto"),
            "legacy false vetoes native death");

        ServerPlayerEntity player = ServerPlayerEntity.of(1L);
        check(player != null && CppModRuntime.dispatchEntityAllowDeath(player, 8.0f, "allowed"),
            "player death gate accepts the player handle");
        int playerBeforeVeto = playerAllow.get();
        check(!CppModRuntime.dispatchEntityAllowDeath(player, 8.0f, "player-veto"),
            "player-specific false vetoes native death");
        check(playerAllow.get() == playerBeforeVeto + 1,
            "player-specific event is only dispatched for a player victim");
        check(CppModRuntime.dispatchEntityAllowDeath(null, 8.0f, "no-victim"),
            "missing native victim does not veto gameplay");

        int modernBeforeException = modernAllow.get();
        check(CppModRuntime.dispatchEntityAllowDeath(mob, 2.0f, "listener-exception"),
            "allow-death listener exception is contained");
        check(modernAllow.get() == modernBeforeException + 1 && allowAfterException.get() == 1,
            "later allow-death callbacks still run after a throwing listener");

        AtomicReference<DamageObservation> modernDamage = new AtomicReference<>();
        AtomicReference<DamageObservation> legacyDamage = new AtomicReference<>();
        AtomicInteger modernAfterDamage = new AtomicInteger();
        AtomicInteger legacyAfterDamage = new AtomicInteger();
        AtomicInteger afterDamageAfterException = new AtomicInteger();
        AtomicInteger modernAfterDeath = new AtomicInteger();
        AtomicInteger legacyAfterDeath = new AtomicInteger();
        AtomicInteger afterDeathAfterException = new AtomicInteger();
        ServerLivingEntityEvents.AFTER_DAMAGE.register((victim, source, base, taken, blocked) -> {
            modernAfterDamage.incrementAndGet();
            modernDamage.set(new DamageObservation(victim, source.getName(), base, taken, blocked));
        });
        ServerLivingEntityEvents.AFTER_DAMAGE.register((victim, source, base, taken, blocked) -> {
            if ("listener-exception".equals(source.getName()))
                throw new IllegalStateException("fixture after-damage failure");
        });
        ServerLivingEntityEvents.AFTER_DAMAGE.register((victim, source, base, taken, blocked) -> {
            if ("listener-exception".equals(source.getName()))
                afterDamageAfterException.incrementAndGet();
        });
        net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AFTER_DAMAGE
            .register((victim, source, base, taken, blocked) -> {
                legacyAfterDamage.incrementAndGet();
                legacyDamage.set(new DamageObservation(victim, source.getName(), base, taken, blocked));
            });
        ServerLivingEntityEvents.AFTER_DEATH.register((victim, source) -> {
            modernAfterDeath.incrementAndGet();
            if ("listener-exception".equals(source.getName()))
                throw new IllegalStateException("fixture after-death failure");
        });
        ServerLivingEntityEvents.AFTER_DEATH.register((victim, source) -> {
            if ("listener-exception".equals(source.getName()))
                afterDeathAfterException.incrementAndGet();
        });
        net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AFTER_DEATH
            .register((victim, source) -> legacyAfterDeath.incrementAndGet());

        CppModRuntime.dispatchEntityDamageApplied(mob, 10.0f, 6.5f,
            true, false, "blocked-hit");
        check(modernAfterDamage.get() == 1 && legacyAfterDamage.get() == 1,
            "modern and legacy after-damage callbacks dispatch");
        check(modernDamage.get().equals(new DamageObservation(mob, "blocked-hit", 10.0f, 6.5f, true))
                && legacyDamage.get().equals(modernDamage.get()),
            "base damage, actual damage, blocked flag, victim, and cause are preserved");
        check(modernAfterDeath.get() == 0 && legacyAfterDeath.get() == 0,
            "after-death callbacks do not run for a surviving victim");

        int modernAfterDamageBeforeDeath = modernAfterDamage.get();
        int legacyAfterDamageBeforeDeath = legacyAfterDamage.get();
        CppModRuntime.dispatchEntityDamageApplied(player, 8.0f, 7.0f,
            false, true, "fatal-hit");
        check(modernAfterDeath.get() == 1 && legacyAfterDeath.get() == 1,
            "modern and legacy after-death callbacks dispatch only after death");
        check(modernAfterDamage.get() == modernAfterDamageBeforeDeath &&
                legacyAfterDamage.get() == legacyAfterDamageBeforeDeath,
            "a lethal hit dispatches after-death without after-damage");

        CppModRuntime.dispatchEntityDamageApplied(mob, 3.0f, 2.0f,
            false, true, "listener-exception");
        check(afterDamageAfterException.get() == 0 &&
                afterDeathAfterException.get() == 1,
            "after-death callback exceptions are contained and later listeners run");

        Method allowDeath = CppModRuntime.class.getMethod("onEntityAllowDeath",
            long.class, long.class, float.class, String.class);
        Method afterDamage = CppModRuntime.class.getMethod("onEntityDamageApplied",
            long.class, long.class, float.class, float.class, boolean.class,
            boolean.class, String.class);
        check(allowDeath.getReturnType() == boolean.class
                && afterDamage.getReturnType() == void.class,
            "native callback methods expose the requested Java descriptors");
        return checks;
    }

    private static void check(boolean condition, String description) {
        checks++;
        if (!condition) throw new AssertionError(description);
    }
}
