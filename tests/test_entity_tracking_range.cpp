#include "../src/game/EntityTrackingBroadcast.hpp"
#include "../src/platform/Socket.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string_view>

using namespace cppfm;

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition) ++failures;
}

void testOfficialRangeTableAlignment() {
    bool aligned = gen::kEntityTrackingRanges1_21_4.size() == 149;
    for (std::size_t i = 0; i < gen::kEntityTrackingRanges1_21_4.size(); ++i) {
        const auto kind = static_cast<MobKind>(i);
        aligned = aligned &&
            gen::kEntityTrackingRanges1_21_4[i].entityId == mobStats(kind).name;
    }
    check(aligned,
          "1.21.4 tracking ranges cover and align with every supported MobKind");
    check(entityTrackingRangeChunks(MobKind::Pig) == 10 &&
              entityTrackingRangeChunks(MobKind::Zombie) == 8 &&
              entityTrackingRangeChunks(MobKind::Cod) == 4 &&
              entityTrackingRangeChunks(MobKind::Warden) == 16 &&
              entityTrackingRangeChunks(MobKind::Item) == 6 &&
              entityTrackingRangeChunks(MobKind::EnderCrystal) == 16,
          "vanilla per-type tracking ranges retain representative values");
}

void testEffectiveRangeAndHorizontalBoundary() {
    check(effectiveEntityTrackingRangeBlocks(MobKind::Zombie, 12) == 128,
          "entity range caps a distant viewer to eight chunks");
    check(effectiveEntityTrackingRangeBlocks(MobKind::Zombie, 4) == 64,
          "client view distance caps the entity range");
    check(effectiveEntityTrackingRangeBlocks(MobKind::Zombie, 12, 50) == 64 &&
              effectiveEntityTrackingRangeBlocks(MobKind::Pig, 12, 50) == 80,
          "vanilla broadcast percentage scales each entity type's range");
    check(effectiveEntityTrackingRangeBlocks(MobKind::Zombie, 12, 25) == 32 &&
              effectiveEntityTrackingRangeBlocks(MobKind::ArmorStand, 12, 25) == 40,
          "25% broadcast preserves exact 32-block zombie and 40-block armor-stand ranges");
    check(effectiveEntityTrackingRangeBlocks(MobKind::Cod, 12, 50) == 32,
          "percentage scales the block distance before applying the view-distance cap");
    check(effectiveEntityTrackingRangeBlocks(MobKind::Zombie, 12, 10) == 12 &&
              effectiveEntityTrackingRangeBlocks(MobKind::Zombie, 12, 1000) == 192,
          "percentage scales block distance with integer truncation and honors configured bounds");
    check(entityTrackingRangeChunks(MobKind::Dolphin) == 5 &&
              effectiveEntityTrackingRangeBlocks(MobKind::Dolphin, 12, 50) == 40 &&
              effectiveEntityTrackingRangeBlocks(MobKind::Dolphin, 12, 10) == 8,
          "five-chunk entity ranges are percentage-scaled in blocks without intermediate chunk rounding");
    check(effectiveEntityTrackingRangeBlocks(MobKind::Marker, 12) == 0,
          "zero-range marker remains at its vanilla tracking boundary");
    check(withinHorizontalEntityTrackingRange(64.0, 0.0, 0.0, 0.0, 64),
          "horizontal tracking boundary is inclusive");
    check(!withinHorizontalEntityTrackingRange(64.001, 0.0, 0.0, 0.0, 64),
          "entity just beyond the tracking boundary is rejected");
    check(!withinHorizontalEntityTrackingRange(46.0, 46.0, 0.0, 0.0, 64),
          "tracking uses horizontal Euclidean distance, not a square radius");
    check(withinHorizontalEntityTrackingRange(0.0, 64.0, 0.0, 0.0, 64),
          "vertical separation does not affect horizontal entity tracking");
    check(!withinHorizontalEntityTrackingRange(
              std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0, 0.0, 64),
          "non-finite entity coordinates cannot become visible");
    check(!withinHorizontalEntityTrackingRange(0.0, 0.0, 0.0, 0.0, -1),
          "negative tracking ranges fail closed");
}

void testPerViewerTrackingTransitions() {
    Player viewer;
    viewer.inPlay = true;
    viewer.spawned = true;
    viewer.conn = std::make_shared<Connection>(platform::invalid_socket);
    viewer.dimension = 0;
    viewer.x = 0.5;
    viewer.z = 0.5;
    viewer.clientSettings.viewDistance = 12;
    viewer.entityTracking.markChunkSent(0, 0, 0);
    viewer.entityTracking.markChunkSent(0, 3, 0);

    const auto spawn = reconcileEntityTracking(
        viewer, 500, 0, 0.5, 0.5, MobKind::Item, 12, 50);
    const auto update = reconcileEntityTracking(
        viewer, 500, 0, 0.5, 0.5, MobKind::Item, 12, 50);
    const auto exactBoundary = reconcileEntityTracking(
        viewer, 500, 0, 48.5, 0.5, MobKind::Item, 12, 50);
    const auto outsideBoundary = reconcileEntityTracking(
        viewer, 500, 0, 48.501, 0.5, MobKind::Item, 12, 50);
    check(spawn.action == EntityTrackingAction::Spawn &&
              update.action == EntityTrackingAction::Update &&
              exactBoundary.action == EntityTrackingAction::Update &&
              outsideBoundary.action == EntityTrackingAction::Remove &&
              !viewer.entityTracking.tracksEntity(500),
          "per-viewer item tracking spawns, updates, and removes at the inclusive range edge");

    viewer.clientSettings.viewDistance = 2;
    viewer.entityTracking.markChunkSent(0, 2, 0);
    const auto clientDistanceCap = reconcileEntityTracking(
        viewer, 501, 0, 32.5, 0.5, MobKind::Zombie, 12, 100);
    const auto beyondClientDistance = reconcileEntityTracking(
        viewer, 501, 0, 32.501, 0.5, MobKind::Zombie, 12, 100);
    check(clientDistanceCap.action == EntityTrackingAction::Spawn &&
              beyondClientDistance.action == EntityTrackingAction::Remove,
          "viewer client view distance caps tracking and still emits a removal transition");

    viewer.clientSettings.viewDistance = 12;
    const auto armorStandBoundary = reconcileEntityTracking(
        viewer, 503, 0, 40.5, 0.5, MobKind::ArmorStand, 12, 25);
    const auto beyondArmorStandBoundary = reconcileEntityTracking(
        viewer, 503, 0, 40.501, 0.5, MobKind::ArmorStand, 12, 25);
    check(armorStandBoundary.action == EntityTrackingAction::Spawn &&
              beyondArmorStandBoundary.action == EntityTrackingAction::Remove,
          "stationary armor stand transitions at its exact inclusive 40-block range");

    const auto zombieBoundary = reconcileEntityTracking(
        viewer, 504, 0, 32.5, 0.5, MobKind::Zombie, 12, 25);
    const auto beyondZombieBoundary = reconcileEntityTracking(
        viewer, 504, 0, 32.501, 0.5, MobKind::Zombie, 12, 25);
    check(zombieBoundary.action == EntityTrackingAction::Spawn &&
              beyondZombieBoundary.action == EntityTrackingAction::Remove,
          "zombie transitions at its exact inclusive 32-block configured range");

    const auto noChunk = reconcileEntityTracking(
        viewer, 502, 0, 16.5, 0.5, MobKind::Item, 12, 100);
    viewer.entityTracking.markChunkSent(0, 1, 0);
    const auto chunkLoaded = reconcileEntityTracking(
        viewer, 502, 0, 16.5, 0.5, MobKind::Item, 12, 100);
    viewer.dimension = -1;
    const auto dimensionChanged = reconcileEntityTracking(
        viewer, 502, 0, 16.5, 0.5, MobKind::Item, 12, 100);
    check(noChunk.action == EntityTrackingAction::None &&
              chunkLoaded.action == EntityTrackingAction::Spawn &&
              dimensionChanged.action == EntityTrackingAction::Remove,
          "entity packets require a sent chunk and same dimension, with despawn on dimension exit");

    check(projectileTrackingKind(ProjectileKind::Arrow) == MobKind::Arrow &&
              projectileTrackingKind(ProjectileKind::ShulkerBullet) ==
                  MobKind::ShulkerBullet &&
              entityTrackingRangeChunks(
                  projectileTrackingKind(ProjectileKind::Arrow)) == 4 &&
              entityTrackingRangeChunks(
                  projectileTrackingKind(ProjectileKind::ShulkerBullet)) == 8,
          "projectile kinds use their generated vanilla entity tracking ranges");
}

void testViewerMovementReconcilesStationaryItems() {
    Player viewer;
    viewer.inPlay = true;
    viewer.spawned = true;
    viewer.conn = std::make_shared<Connection>(platform::invalid_socket);
    viewer.dimension = 0;
    viewer.x = 0.5;
    viewer.z = 0.5;
    viewer.clientSettings.viewDistance = 12;
    viewer.entityTracking.markChunkSent(0, 0, 0);

    // Entity coordinates remain fixed; only the viewer moves. Keep the
    // entity's chunk marked sent so this specifically exercises distance.
    const auto itemSpawn = reconcileEntityTracking(
        viewer, 600, 0, 0.5, 0.5, MobKind::Item, 12, 50);
    const auto orbSpawn = reconcileEntityTracking(
        viewer, 601, 0, 0.5, 0.5, MobKind::ExperienceOrb, 12, 50);
    viewer.x = 49.001;
    const auto itemRemove = reconcileEntityTracking(
        viewer, 600, 0, 0.5, 0.5, MobKind::Item, 12, 50);
    const auto orbRemove = reconcileEntityTracking(
        viewer, 601, 0, 0.5, 0.5, MobKind::ExperienceOrb, 12, 50);
    viewer.x = 48.5;
    const auto itemRespawn = reconcileEntityTracking(
        viewer, 600, 0, 0.5, 0.5, MobKind::Item, 12, 50);
    const auto orbRespawn = reconcileEntityTracking(
        viewer, 601, 0, 0.5, 0.5, MobKind::ExperienceOrb, 12, 50);
    check(itemSpawn.action == EntityTrackingAction::Spawn &&
              orbSpawn.action == EntityTrackingAction::Spawn &&
              itemRemove.action == EntityTrackingAction::Remove &&
              orbRemove.action == EntityTrackingAction::Remove &&
              itemRespawn.action == EntityTrackingAction::Spawn &&
              orbRespawn.action == EntityTrackingAction::Spawn,
          "viewer-only movement spawns/removes stationary items and XP orbs at the range boundary");
}

void testChunkCoordinatesFailClosed() {
    std::int32_t chunk = 0;
    check(entityChunkCoordinate(-0.01, chunk) && chunk == -1,
          "negative positions use floor-based chunk coordinates");
    check(!entityChunkCoordinate(std::numeric_limits<double>::infinity(), chunk) &&
              !entityChunkCoordinate(
                  (static_cast<double>(
                       std::numeric_limits<std::int32_t>::max()) + 1.0) *
                      16.0,
                  chunk),
          "non-finite and out-of-range chunk positions fail closed");
}
} // namespace

int main() {
    testOfficialRangeTableAlignment();
    testEffectiveRangeAndHorizontalBoundary();
    testPerViewerTrackingTransitions();
    testViewerMovementReconcilesStationaryItems();
    testChunkCoordinatesFailClosed();
    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
