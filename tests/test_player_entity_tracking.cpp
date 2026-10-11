#include "../src/game/ClientEntityTracking.hpp"
#include "../src/game/PacketOrder.hpp"
#include "../src/game/PlayerTrackingRules.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <vector>

using namespace cppfm;

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition) ++failures;
}

void testVanillaPlayerRange() {
    check(kVanillaPlayerTrackingRangeChunks == 32,
          "1.21.4 player entity type uses a 32-chunk baseline range");
    check(effectivePlayerTrackingRangeBlocks(10, 10, 100) == 160,
          "viewer effective view distance caps the default player range");
    check(effectivePlayerTrackingRangeBlocks(32, 8, 100) == 128,
          "client view distance caps a 32-chunk server setting");
    check(effectivePlayerTrackingRangeBlocks(32, 32, 100) == 512,
          "player baseline range is converted to blocks");
    check(effectivePlayerTrackingRangeBlocks(32, 32, 50) == 256,
          "broadcast percentage scales before applying the view cap");
    check(effectivePlayerTrackingRangeBlocks(32, 32, 10) == 51,
          "percentage uses vanilla block-distance integer truncation");
    check(effectivePlayerTrackingRangeBlocks(32, 32, 1000) == 512,
          "large multiplier remains capped by viewer view distance");
}

void testVanillaVisibilityPredicate() {
    constexpr int range = 128;
    check(withinHorizontalPlayerTrackingRange(128.0, 0.0, 0.0, 0.0, range),
          "horizontal tracking boundary is inclusive");
    check(!withinHorizontalPlayerTrackingRange(
              128.001, 0.0, 0.0, 0.0, range),
          "position beyond the radius is hidden");
    check(!withinHorizontalPlayerTrackingRange(
              100.0, 100.0, 0.0, 0.0, range),
          "tracking radius is a circle rather than a square");
    check(withinHorizontalPlayerTrackingRange(
              0.0, 128.0, 0.0, 0.0, range),
          "vertical separation does not affect tracking distance");
    check(!withinHorizontalPlayerTrackingRange(
              std::numeric_limits<double>::infinity(), 0.0, 0.0, 0.0,
              range),
          "non-finite coordinates fail closed");

    PlayerTrackingCandidate candidate{
        .isSelf = false,
        .viewerInPlay = true,
        .subjectInPlay = true,
        .sameDimension = true,
        .subjectChunkSent = true,
        .viewerX = 0.0,
        .viewerZ = 0.0,
        .subjectX = 128.0,
        .subjectZ = 0.0,
        .rangeBlocks = range,
    };
    check(shouldTrackPlayerEntity(candidate),
          "live same-dimension peer in a sent chunk is visible");
    candidate.isSelf = true;
    check(!shouldTrackPlayerEntity(candidate), "viewer never tracks itself");
    candidate.isSelf = false;
    candidate.sameDimension = false;
    check(!shouldTrackPlayerEntity(candidate),
          "different dimensions cannot see each other's player entities");
    candidate.sameDimension = true;
    candidate.subjectChunkSent = false;
    check(!shouldTrackPlayerEntity(candidate),
          "a sent target chunk is required in addition to distance");
    candidate.subjectChunkSent = true;
    candidate.viewerInPlay = false;
    check(!shouldTrackPlayerEntity(candidate),
          "inactive viewers do not retain entity visibility");
}

void testPerViewerTransitions() {
    ClientEntityTracking viewerA;
    ClientEntityTracking viewerB;
    viewerA.markChunkSent(0, 0, 0);

    check(viewerA.reconcileEntity(77, true, 0, 0, 0) ==
              ClientEntityTracking::EntityTransition::Spawned,
          "first viewer records one spawn transition");
    check(viewerA.reconcileEntity(77, true, 0, 0, 0) ==
              ClientEntityTracking::EntityTransition::Unchanged,
          "repeated visibility does not duplicate a spawn");
    check(viewerB.reconcileEntity(77, true, 0, 0, 0) ==
              ClientEntityTracking::EntityTransition::Unchanged,
          "second viewer has independent chunk/tracking state");

    viewerB.markChunkSent(0, 0, 0);
    check(viewerB.reconcileEntity(77, true, 0, 0, 0) ==
              ClientEntityTracking::EntityTransition::Spawned,
          "second viewer independently records its spawn");
    viewerA.markChunkSent(0, 1, 0);
    check(viewerA.reconcileEntity(77, true, 0, 1, 0) ==
              ClientEntityTracking::EntityTransition::Moved,
          "moving an entity between sent chunks does not respawn it");
    check(viewerB.reconcileEntity(77, true, 0, 1, 0) ==
              ClientEntityTracking::EntityTransition::Despawned,
          "missing destination chunk causes a per-viewer despawn");
    check(viewerA.forgetChunk(0, 1, 0) == std::vector<std::int32_t>{77} &&
              !viewerA.tracksEntity(77),
          "chunk eviction clears the entity from that viewer only");
    check(!viewerB.tracksEntity(77),
          "one viewer's transitions cannot mutate another viewer's map");
}

void testChunkAndDimensionLifecycle() {
    ClientEntityTracking viewer;
    viewer.markChunkSent(0, -2, 7);
    check(viewer.reconcileEntity(91, true, 0, -2, 7) ==
              ClientEntityTracking::EntityTransition::Spawned,
          "player spawns only after its destination chunk is sent");

    const auto removed = viewer.forgetChunk(0, -2, 7);
    check(removed == std::vector<std::int32_t>{91} &&
              !viewer.hasChunk(0, -2, 7) && !viewer.tracksEntity(91),
          "chunk unload retires its tracked players before forgetting the chunk");
    viewer.markChunkSent(0, -2, 7);
    check(viewer.reconcileEntity(91, true, 0, -2, 7) ==
              ClientEntityTracking::EntityTransition::Spawned,
          "a re-sent chunk allows a fresh player spawn");

    // Respawn/dimension transfer resets the viewer's client world. The native
    // session clears both sent chunks and entity state before reconciling anew.
    viewer.clear();
    check(viewer.sentChunks().empty() && !viewer.tracksEntity(91),
          "dimension reset clears source-world player visibility");
    viewer.markChunkSent(-1, -2, 7);
    check(viewer.reconcileEntity(91, true, -1, -2, 7) ==
              ClientEntityTracking::EntityTransition::Spawned,
          "destination-world player spawn is independent of source-world state");

    check(requiresWorldStateOrdering(proto::pl::sc::SpawnEntity) &&
              requiresWorldStateOrdering(proto::pl::sc::SetEntityMetadata) &&
              requiresWorldStateOrdering(proto::pl::sc::SetEquipment) &&
              requiresWorldStateOrdering(proto::pl::sc::RemoveEntities),
          "spawn, initial state, and removal share ordered world-state output");
}

void testDisconnectAndDuplicateLoginTransitions() {
    ClientEntityTracking viewer;
    viewer.markChunkSent(0, 3, 4);
    check(viewer.reconcileEntity(101, true, 0, 3, 4) ==
              ClientEntityTracking::EntityTransition::Spawned,
          "incumbent session becomes visible to this viewer");
    check(viewer.reconcileEntity(101, false, 0, 3, 4) ==
              ClientEntityTracking::EntityTransition::Despawned &&
              !viewer.tracksEntity(101),
          "disconnect or duplicate-login retirement removes the incumbent entity");
    check(viewer.reconcileEntity(202, true, 0, 3, 4) ==
              ClientEntityTracking::EntityTransition::Spawned &&
              viewer.tracksEntity(202) && !viewer.tracksEntity(101),
          "replacement login is a fresh viewer-specific spawn after old-ID removal");
}

void testChunkReverseIndexAcrossMoves() {
    ClientEntityTracking viewer;
    viewer.markChunkSent(0, 4, -3);
    viewer.markChunkSent(0, 5, -3);
    bool allSpawned = true;
    for (std::int32_t id = 1000; id < 1100; ++id) {
        allSpawned &= viewer.reconcileEntity(id, true, 0, 4, -3) ==
                      ClientEntityTracking::EntityTransition::Spawned;
    }
    check(allSpawned, "entities are indexed in their initial chunk");
    check(viewer.reconcileEntity(1007, true, 0, 5, -3) ==
              ClientEntityTracking::EntityTransition::Moved,
          "entity movement updates the per-chunk reverse index");
    check(viewer.updateTrackedEntityChunk(1008, 0, 5, -3),
          "explicit chunk update uses the same reverse-index invariant");
    check(viewer.forgetEntity(1010) && !viewer.tracksEntity(1010),
          "explicit entity removal also leaves no stale chunk-index entry");

    const auto removedFirst = viewer.forgetChunk(0, 4, -3);
    check(removedFirst.size() == 97 && removedFirst.front() == 1000 &&
              removedFirst.back() == 1099 &&
              !std::binary_search(removedFirst.begin(), removedFirst.end(), 1010),
          "eviction removes only entities still indexed in the evicted chunk");
    check(viewer.tracksEntity(1007) && viewer.tracksEntity(1008),
          "entities moved before source eviction remain tracked");

    const auto removedSecond = viewer.forgetChunk(0, 5, -3);
    check(removedSecond == std::vector<std::int32_t>{1007, 1008} &&
              !viewer.tracksEntity(1007) && !viewer.tracksEntity(1008),
          "destination eviction removes moved entities from their new bucket");
}
} // namespace

int main() {
    testVanillaPlayerRange();
    testVanillaVisibilityPredicate();
    testPerViewerTransitions();
    testChunkAndDimensionLifecycle();
    testDisconnectAndDuplicateLoginTransitions();
    testChunkReverseIndexAcrossMoves();
    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
