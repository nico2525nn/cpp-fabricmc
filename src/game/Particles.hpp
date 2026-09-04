#pragma once
#include <cstdint>
#include "../core/ByteBuffer.hpp"

namespace cppfm {

// Particle type ids from prismarine 1.21.4 protocol.json mapper (112 ids)
namespace ParticleId {
    constexpr int angry_villager = 0;
    constexpr int block = 1;
    constexpr int block_marker = 2;
    constexpr int crit = 5;               // vanilla crit (melee critical particles)
    constexpr int damage_indicator = 6;   // vanilla damage_indicator (sweep/thorns ticks)
    constexpr int dust = 13;
    constexpr int dust_color_transition = 14;
    constexpr int entity_effect = 20;
    constexpr int explosion_emitter = 21;
    constexpr int explosion = 22;
    constexpr int sonic_boom = 27;
    constexpr int falling_dust = 28;
    constexpr int pale_oak_leaves = 34;
    constexpr int sculk_charge = 36;
    constexpr int item = 45;
    constexpr int vibration = 46;
    constexpr int trail = 47;
    constexpr int portal = 56;
    constexpr int sweep_attack = 63;      // vanilla sweep_attack (sweeping edge particles)
    constexpr int enchanted_hit = 17;     // vanilla enchanted_hit (thorns/crit feedback)
    constexpr int dust_pillar = 107;
    constexpr int block_crumble = 111;
}

struct ParticleData {
    std::uint32_t blockState = 0;
    float r = 1.0f, g = 0.0f, b = 0.0f;
    float scale = 1.0f;
    float fromR = 1.0f, fromG = 0.0f, fromB = 0.0f;
    float toR = 0.0f, toG = 1.0f, toB = 0.0f;
    std::int32_t color = 0;
    float roll = 0.0f;
    std::int32_t shriekDelay = 0;
    // provided convenience: set dust from ARGB 0xAARRGGBB
    void setDustFromARGB(std::int32_t argb, float s = 1.0f){
        float rf = ((argb >> 16) & 0xFF) / 255.0f;
        float gf = ((argb >> 8) & 0xFF) / 255.0f;
        float bf = (argb & 0xFF) / 255.0f;
        r = rf; g = gf; b = bf; scale = s;
    }
};

// Write particle container: varint type + per-type switch data into out (out is the tail of world_particles after amount)
// Caller must have already written longDistance, alwaysShow, x,y,z, offsets, speed, amount.
inline void writeParticlePayload(WriteBuffer& out, int particleId, const ParticleData& d = {}){
    out.varint(particleId);
    switch(particleId){
        case ParticleId::block:
        case ParticleId::block_marker:
        case ParticleId::falling_dust:
        case ParticleId::dust_pillar:
        case ParticleId::block_crumble:
            out.varint(static_cast<std::int32_t>(d.blockState));
            break;
        case ParticleId::dust:
            out.f32(d.r); out.f32(d.g); out.f32(d.b); out.f32(d.scale);
            break;
        case ParticleId::dust_color_transition:
            out.f32(d.fromR); out.f32(d.fromG); out.f32(d.fromB); out.f32(d.scale);
            out.f32(d.toR); out.f32(d.toG); out.f32(d.toB);
            break;
        case ParticleId::entity_effect:
            out.i32(d.color);
            break;
        case ParticleId::sculk_charge:
            out.f32(d.roll);
            break;
        case 101: // shriek
            out.varint(d.shriekDelay);
            break;
        case ParticleId::item: {
            // Item particle expects Slot (varint count + itemId + components). For now write empty (air) if not provided. Caller can
            // provide Slot via d.blockState? We keep as air: count 0 To send a real item, caller should write Slot directly after this
            // helper; we provide a no-op for generic. Minimal air slot:
            out.varint(0);
            break;
        }
        // vibration(46) and trail(47) are complex (positionType etc). No-op for simple cases.
        default:
            break;
    }
}

// Build full world_particles packet body (excluding outer packet id 0x2A which is handled by sendPacket)
inline WriteBuffer makeWorldParticlesBody(double x, double y, double z,
                                          float offX, float offY, float offZ,
                                          float speed, std::int32_t amount,
                                          int particleId, const ParticleData& data = {},
                                          bool longDistance = false, bool alwaysShow = false){
    WriteBuffer out;
    out.boolean(longDistance);
    out.boolean(alwaysShow);
    out.f64(x); out.f64(y); out.f64(z);
    out.f32(offX); out.f32(offY); out.f32(offZ);
    out.f32(speed);
    out.i32(amount);
    writeParticlePayload(out, particleId, data);
    return out;
}

// Convenience for pale_oak_leaves ambience (Simple, count 1, no data)
inline WriteBuffer makePaleOakLeavesBody(double x, double y, double z){
    return makeWorldParticlesBody(x, y, z, 0,0,0, 0, 1, ParticleId::pale_oak_leaves, {}, false, false);
}

} // namespace cppfm
