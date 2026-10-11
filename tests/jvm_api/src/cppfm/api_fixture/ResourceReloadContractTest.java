package cppfm.api_fixture;

import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import net.fabricmc.fabric.api.resource.IdentifiableResourceReloadListener;
import net.fabricmc.fabric.api.resource.ResourceManagerHelper;
import net.fabricmc.fabric.api.resource.SimpleResourceReloadListener;
import net.minecraft.resource.ResourceManager;
import net.minecraft.resource.ResourceType;
import net.minecraft.server.MinecraftServer;
import net.minecraft.util.Identifier;

/** Deterministic contract tests for the server's two-phase Fabric reload path. */
final class ResourceReloadContractTest {
    private static final Identifier RESOURCE_ID =
        Identifier.of("cppfm_api_fixture", "reload.json");
    private static final Identifier A_ID = Identifier.of("cppfm_api_fixture", "a");
    private static final Identifier B_ID = Identifier.of("cppfm_api_fixture", "b");
    private static final Identifier FAILURE_ID = Identifier.of("cppfm_api_fixture", "failure");
    private static final AtomicReference<RunControl> CURRENT = new AtomicReference<>();
    private static int checks;

    private ResourceReloadContractTest() { }

    static int run(MinecraftServer server) throws Exception {
        Path resources = Files.createTempDirectory("cppfm-reload-contract-");
        Path sentinel = resources.resolve("assets/data/cppfm_api_fixture/reload.json");
        Files.createDirectories(sentinel.getParent());
        Files.writeString(sentinel, "reload-resource-live");
        String previousUserDir = System.getProperty("user.dir");
        int endBefore = ApiSurfaceFixture.DATA_PACK_RELOAD_END.get();
        int successBefore = ApiSurfaceFixture.DATA_PACK_RELOAD_SUCCESS.get();
        Thread originalOwner = Thread.currentThread();
        try {
            System.setProperty("user.dir", resources.toString());
            ResourceManagerHelper.get(ResourceType.SERVER_DATA).registerReloadListener(
                new ControlledListener("a"));
            ResourceManagerHelper.get(ResourceType.SERVER_DATA).registerReloadListener(
                new ControlledListener("b"));
            ResourceManagerHelper.get(ResourceType.SERVER_DATA).registerReloadListener(
                new ControlledListener("failure"));

            RunControl concurrent = RunControl.concurrentPrepare();
            CURRENT.set(concurrent);
            OwnerReload successful = startOwnerReload(server);
            check(successful.returned.await(2, TimeUnit.SECONDS),
                "server owner returns from reload while prepare futures remain pending");
            check(concurrent.loadsStarted.await(3, TimeUnit.SECONDS),
                "both independent listeners and the third listener start load concurrently");
            check(successful.future.get() != null && !successful.future.get().isDone(),
                "reload future stays pending while manual prepare gates are closed");
            check(concurrent.loadThreads.stream().noneMatch(thread -> thread == successful.thread),
                "all load stages execute away from the MinecraftServer owner thread");

            concurrent.loadGates.get("a").complete("a-data");
            Thread.sleep(40);
            check(concurrent.applyOrder.isEmpty(),
                "no listener applies until every listener reaches the prepare barrier");
            concurrent.loadGates.get("b").complete("b-data");
            check(successful.awaitFinished(5, TimeUnit.SECONDS),
                "successful reload settles after prepare and owner-thread apply tasks");
            check(successful.failure.get() == null && !successful.future.get().isCompletedExceptionally(),
                "successful two-phase reload completes without failure");
            check(concurrent.applyOrder.indexOf("a") >= 0 &&
                    concurrent.applyOrder.indexOf("a") < concurrent.applyOrder.indexOf("b"),
                "declared listener dependency orders B apply after A apply");
            check(concurrent.applyThreads.size() == 3 &&
                    concurrent.applyThreads.stream().allMatch(thread -> thread == successful.thread),
                "all apply callbacks run on the MinecraftServer owner thread");
            check(ApiSurfaceFixture.DATA_PACK_RELOAD_END.get() == endBefore + 1 &&
                    ApiSurfaceFixture.DATA_PACK_RELOAD_SUCCESS.get() == successBefore + 1,
                "successful reload emits one successful end event after listeners settle");
            check(ApiSurfaceFixture.DATA_PACK_RELOAD_THREAD.get() == successful.thread,
                "reload lifecycle events stay on the MinecraftServer owner thread");
            ResourceManager closedAfterSuccess = ApiSurfaceFixture.DATA_PACK_RELOAD_MANAGER.get();
            check(closedAfterSuccess != null && closedAfterSuccess.getResource(RESOURCE_ID).isEmpty(),
                "resource manager is closed only after success finalization completes");

            int failureEndBefore = ApiSurfaceFixture.DATA_PACK_RELOAD_END.get();
            RunControl failure = RunControl.concurrentApplyFailure();
            CURRENT.set(failure);
            OwnerReload failed = startOwnerReload(server);
            check(failed.returned.await(2, TimeUnit.SECONDS) &&
                    failure.loadsStarted.await(3, TimeUnit.SECONDS),
                "failing reload returns immediately and starts every prepare stage");
            check(failure.applyStarted.await(3, TimeUnit.SECONDS),
                "failure scenario reaches all apply stages after the shared barrier");
            failure.applyGates.get("failure").completeExceptionally(
                new IllegalStateException("fixture listener failure"));
            Thread.sleep(40);
            ResourceManager stillOpen = ApiSurfaceFixture.DATA_PACK_RELOAD_MANAGER.get();
            check(!failed.future.get().isDone() &&
                    ApiSurfaceFixture.DATA_PACK_RELOAD_END.get() == failureEndBefore,
                "one failed listener cannot finalize while a sibling apply future is pending");
            check(readSentinel(stillOpen),
                "resource manager remains usable until every listener future settles");

            failure.applyGates.get("b").complete(null);
            check(failed.awaitFinished(5, TimeUnit.SECONDS),
                "failed reload settles only after the sibling listener completes");
            check(failed.future.get().isCompletedExceptionally(),
                "listener apply failure propagates to MinecraftServer.reloadResources");
            check(ApiSurfaceFixture.DATA_PACK_RELOAD_END.get() == failureEndBefore + 1 &&
                    ApiSurfaceFixture.DATA_PACK_RELOAD_SUCCESS.get() == successBefore + 1,
                "failed reload emits exactly one unsuccessful end event");
            ResourceManager closedAfterFailure = ApiSurfaceFixture.DATA_PACK_RELOAD_MANAGER.get();
            check(closedAfterFailure != null &&
                    closedAfterFailure.getResource(RESOURCE_ID).isEmpty(),
                "resource manager closes after failed listeners have all settled");
        } finally {
            CURRENT.set(null);
            server.adoptCurrentThread();
            if (previousUserDir == null) System.clearProperty("user.dir");
            else System.setProperty("user.dir", previousUserDir);
            try (var paths = Files.walk(resources)) {
                for (Path path : paths.sorted(java.util.Comparator.reverseOrder()).toList())
                    Files.deleteIfExists(path);
            }
        }
        return checks;
    }

    private static OwnerReload startOwnerReload(MinecraftServer server) {
        OwnerReload run = new OwnerReload();
        run.thread = new Thread(() -> {
            server.adoptCurrentThread();
            try {
                run.future.set(server.reloadResources(List.of()));
            } catch (Throwable failure) {
                run.failure.set(failure);
            } finally {
                run.returned.countDown();
            }
            CompletableFuture<Void> reload = run.future.get();
            if (reload == null) return;
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(12);
            while (!reload.isDone() && System.nanoTime() < deadline) {
                server.runTasksTillTickEnd();
                try { Thread.sleep(1); }
                catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    run.failure.compareAndSet(null, interrupted);
                    return;
                }
            }
            server.runTasksTillTickEnd();
            if (!reload.isDone()) run.failure.compareAndSet(null,
                new AssertionError("resource reload did not settle before fixture deadline"));
        }, "cppfm-reload-game-thread-fixture");
        run.thread.start();
        return run;
    }

    private static boolean readSentinel(ResourceManager manager) throws Exception {
        if (manager == null) return false;
        var resource = manager.getResource(RESOURCE_ID);
        if (resource.isEmpty()) return false;
        try (var view = resource.orElseThrow(); InputStream input = view.getInputStream()) {
            return input.read() == 'r';
        }
    }

    private static void check(boolean condition, String label) {
        checks++;
        if (!condition) throw new AssertionError(label);
    }

    private static final class ControlledListener implements SimpleResourceReloadListener<String> {
        private final String name;

        private ControlledListener(String name) { this.name = name; }

        @Override public Identifier getFabricId() {
            return switch (name) {
                case "a" -> A_ID;
                case "b" -> B_ID;
                default -> FAILURE_ID;
            };
        }

        @Override public List<Identifier> getFabricDependencies() {
            return name.equals("b") ? List.of(A_ID) : List.of();
        }

        @Override public CompletableFuture<String> load(ResourceManager manager, Executor executor) {
            RunControl control = CURRENT.get();
            if (control == null) return CompletableFuture.failedFuture(
                new IllegalStateException("reload fixture has no run control"));
            return CompletableFuture.supplyAsync(() -> {
                control.loadThreads.add(Thread.currentThread());
                control.loadsStarted.countDown();
                return name;
            }, executor).thenCompose(control.loadGates::get);
        }

        @Override public CompletableFuture<Void> apply(String prepared, ResourceManager manager,
                                                       Executor executor) {
            RunControl control = CURRENT.get();
            if (control == null) return CompletableFuture.failedFuture(
                new IllegalStateException("reload fixture has no run control"));
            control.applyThreads.add(Thread.currentThread());
            control.applyOrder.add(name);
            control.applyStarted.countDown();
            try {
                if (!readSentinel(manager)) return CompletableFuture.failedFuture(
                    new IllegalStateException("reload resource unavailable during apply"));
            } catch (Exception failure) {
                return CompletableFuture.failedFuture(failure);
            }
            return control.applyGates.get(name);
        }
    }

    private static final class RunControl {
        private final Map<String, CompletableFuture<String>> loadGates = new ConcurrentHashMap<>();
        private final Map<String, CompletableFuture<Void>> applyGates = new ConcurrentHashMap<>();
        private final CountDownLatch loadsStarted = new CountDownLatch(3);
        private final CountDownLatch applyStarted = new CountDownLatch(3);
        private final List<Thread> loadThreads = new java.util.concurrent.CopyOnWriteArrayList<>();
        private final List<Thread> applyThreads = new java.util.concurrent.CopyOnWriteArrayList<>();
        private final List<String> applyOrder = new java.util.concurrent.CopyOnWriteArrayList<>();

        private static RunControl concurrentPrepare() {
            RunControl control = new RunControl();
            control.loadGates.put("a", new CompletableFuture<>());
            control.loadGates.put("b", new CompletableFuture<>());
            control.loadGates.put("failure", CompletableFuture.completedFuture("failure"));
            control.completedApplies();
            return control;
        }

        private static RunControl concurrentApplyFailure() {
            RunControl control = new RunControl();
            for (String name : List.of("a", "b", "failure"))
                control.loadGates.put(name, CompletableFuture.completedFuture(name));
            control.applyGates.put("a", CompletableFuture.completedFuture(null));
            control.applyGates.put("b", new CompletableFuture<>());
            control.applyGates.put("failure", new CompletableFuture<>());
            return control;
        }

        private void completedApplies() {
            for (String name : List.of("a", "b", "failure"))
                applyGates.put(name, CompletableFuture.completedFuture(null));
        }
    }

    private static final class OwnerReload {
        private final CountDownLatch returned = new CountDownLatch(1);
        private final AtomicReference<CompletableFuture<Void>> future = new AtomicReference<>();
        private final AtomicReference<Throwable> failure = new AtomicReference<>();
        private Thread thread;

        private boolean awaitFinished(long timeout, TimeUnit unit) throws InterruptedException {
            thread.join(unit.toMillis(timeout));
            return !thread.isAlive() && failure.get() == null && future.get() != null &&
                future.get().isDone();
        }
    }
}
