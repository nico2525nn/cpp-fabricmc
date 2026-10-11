package cppfm.vendor.probe;

import java.net.URL;
import java.security.CodeSource;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicInteger;

import net.fabricmc.api.ModInitializer;
import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;

/** A third-party-style mod that links directly to Fabric API's Event type. */
public final class FabricApiDependentMod implements ModInitializer {
    @Override
    public void onInitialize() {
        CodeSource codeSource = Event.class.getProtectionDomain().getCodeSource();
        URL location = codeSource == null ? null : codeSource.getLocation();
        String source = location == null ? "" : location.toExternalForm();
        String normalized = source.toLowerCase(Locale.ROOT);
        boolean officialApi = normalized.contains("fabric-api-base");
        boolean shadowFallback = normalized.contains("shadow-classes");
        if (!officialApi && !shadowFallback) {
            throw new IllegalStateException("Fabric API Event came from unexpected source: " + source);
        }

        boolean apiExpected = Boolean.getBoolean("cppfm.official.fabric-api-present");
        if (apiExpected != officialApi) {
            throw new IllegalStateException("Fabric API source mismatch: expected officialApi="
                    + apiExpected + " but loaded from " + source);
        }
        System.out.println("CPPFM_FABRIC_API_CLASS_SOURCE="
                + (officialApi ? "official-api" : "shadow-fallback"));

        AtomicInteger calls = new AtomicInteger();
        Event<Runnable> event = EventFactory.createArrayBacked(
                Runnable.class,
                listeners -> () -> {
                    for (Runnable listener : listeners) listener.run();
                });
        event.register(calls::incrementAndGet);
        event.invoker().run();
        if (calls.get() != 1) {
            throw new IllegalStateException("Fabric API Event dispatched " + calls.get() + " times");
        }
        System.out.println("CPPFM_FABRIC_API_EVENT_FUNCTIONAL");
        System.out.println("CPPFM_FABRIC_API_DEPENDENT_FIXTURE_LOADED");
    }
}
