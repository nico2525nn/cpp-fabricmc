// Packet IDs for Minecraft Java 1.21.4 (protocol 769).
// Verified against community protocol documentation (PrismarineJS minecraft-data)
// and against live wire captures of a reference server.
#pragma once
#include <cstdint>

namespace cppfm::proto {

constexpr std::int32_t kProtocolVersion = 769;
constexpr const char* kMinecraftVersion = "1.21.4";

namespace hb { // handshaking
namespace cs {
constexpr std::uint8_t Intention = 0x00;
}
}

namespace st { // status
namespace cs {
constexpr std::uint8_t Request = 0x00;
constexpr std::uint8_t Ping = 0x01;
}
namespace sc {
constexpr std::uint8_t Response = 0x00;
constexpr std::uint8_t Pong = 0x01;
}
}

namespace lo { // login
namespace cs {
constexpr std::uint8_t Hello = 0x00;              // login start
constexpr std::uint8_t Key = 0x01;                // encryption response
constexpr std::uint8_t CustomQueryAnswer = 0x02;
constexpr std::uint8_t LoginAcknowledged = 0x03;
}
namespace sc {
constexpr std::uint8_t Disconnect = 0x00;
constexpr std::uint8_t GameProfile = 0x02;        // login success
constexpr std::uint8_t SetCompression = 0x03;     // we never send this
}
}

namespace cf { // configuration
namespace cs {
constexpr std::uint8_t ClientInformation = 0x00;
constexpr std::uint8_t CookieResponse = 0x01;
constexpr std::uint8_t CustomPayload = 0x02;
constexpr std::uint8_t FinishAcknowledgement = 0x03;
constexpr std::uint8_t KeepAlive = 0x04;
constexpr std::uint8_t Pong = 0x05;
constexpr std::uint8_t ResourcePackResponse = 0x06;
constexpr std::uint8_t SelectKnownPacks = 0x07;
}
namespace sc {
constexpr std::uint8_t CustomPayload = 0x01;
constexpr std::uint8_t Disconnect = 0x02;
constexpr std::uint8_t FinishConfiguration = 0x03;
constexpr std::uint8_t KeepAlive = 0x04;
constexpr std::uint8_t Ping = 0x05;
constexpr std::uint8_t RegistryData = 0x07;
constexpr std::uint8_t UpdateTags = 0x0D;
constexpr std::uint8_t SelectKnownPacks = 0x0E;
}
}

namespace pl { // play
namespace cs {
constexpr std::uint8_t AcceptTeleportation = 0x00;
constexpr std::uint8_t ChatCommand = 0x05;
constexpr std::uint8_t ChatMessage = 0x07;
constexpr std::uint8_t ChunkBatchReceived = 0x09;
constexpr std::uint8_t ClientCommand = 0x0A;
constexpr std::uint8_t ClientTickEnd = 0x0B;
constexpr std::uint8_t PlayerInput = 0x29;
constexpr std::uint8_t KeepAlive = 0x1A;
constexpr std::uint8_t MovePlayerPos = 0x1C;
constexpr std::uint8_t MovePlayerPosRot = 0x1D;
constexpr std::uint8_t MovePlayerRot = 0x1E;
constexpr std::uint8_t MovePlayerStatusOnly = 0x1F;
constexpr std::uint8_t MoveVehicle = 0x20;
constexpr std::uint8_t PingRequest = 0x24;
constexpr std::uint8_t PlayerLoaded = 0x2A;
constexpr std::uint8_t ChangeDifficulty = 0x03;
constexpr std::uint8_t HeldItemSlot = 0x33;
constexpr std::uint8_t UseEntity = 0x18;
constexpr std::uint8_t PlayerAction = 0x27;
constexpr std::uint8_t UseItemOn = 0x3C;
constexpr std::uint8_t UseItem = 0x3D;
constexpr std::uint8_t Swing = 0x3A;
constexpr std::uint8_t SetCreativeModeSlot = 0x36;
constexpr std::uint8_t SignUpdate = 0x39;
constexpr std::uint8_t Animate = 0x3A; // alias of Swing
}
namespace sc {
constexpr std::uint8_t BundleDelimiter = 0x00;
constexpr std::uint8_t SpawnEntity = 0x01;
constexpr std::uint8_t Animation = 0x03;
constexpr std::uint8_t AckBlockChange = 0x05;
constexpr std::uint8_t BlockUpdate = 0x09;
constexpr std::uint8_t ChunkBatchFinished = 0x0C;
constexpr std::uint8_t ChunkBatchStart = 0x0D;
constexpr std::uint8_t LevelChunkWithLight = 0x28;
constexpr std::uint8_t ForgetLevelChunk = 0x22;
constexpr std::uint8_t KeepAlive = 0x27;
constexpr std::uint8_t Login = 0x2C;             // join game
constexpr std::uint8_t PlayerInfoRemove = 0x3F;
constexpr std::uint8_t PlayerInfoUpdate = 0x40;
constexpr std::uint8_t PlayerPosition = 0x42;    // synchronize position
constexpr std::uint8_t Respawn = 0x4C;
constexpr std::uint8_t SetDefaultSpawn = 0x5B;
constexpr std::uint8_t SetCenterChunk = 0x58;
constexpr std::uint8_t SetHeldSlot = 0x63;
constexpr std::uint8_t SetHealth = 0x62;
constexpr std::uint8_t DeclareCommands = 0x11;
constexpr std::uint8_t SetTime = 0x6B;
constexpr std::uint8_t CustomPayload = 0x19;
constexpr std::uint8_t SystemChat = 0x73;
constexpr std::uint8_t Disconnect = 0x1D;
constexpr std::uint8_t Abilities = 0x3A;
constexpr std::uint8_t ContainerSetContent = 0x13; // window items
constexpr std::uint8_t SetEntityMetadata = 0x5D;
constexpr std::uint8_t EntityTeleport = 0x77;
constexpr std::uint8_t MoveEntityPos = 0x2F;
constexpr std::uint8_t EntityLook = 0x32;
constexpr std::uint8_t MoveEntityPosRot = 0x30;
constexpr std::uint8_t RotateHead = 0x4D;
constexpr std::uint8_t RemoveEntities = 0x47;
constexpr std::uint8_t SetExperience = 0x61;
constexpr std::uint8_t GameEvent = 0x23;
constexpr std::uint8_t SoundEffect = 0x6F;
}
}

} // namespace cppfm::proto
