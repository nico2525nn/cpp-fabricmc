// Packet IDs for Minecraft Java 1.21.4 (protocol 769).
// Verified against PrismarineJS minecraft-data 1.21.4 protocol.json (version-
// pinned) + live wire captures (docs/PROTOCOL_NOTES.md). Strict 1.21.4:
// TradeList 0x2E (not 0x2D), ContainerSetContent 0x13 (not 0x12), OpenScreen
// 0x35 (not 0x34), KeepAlive 0x27 (not 0x26), UpdateLight 0x2B, LevelChunkWithLight 0x28 — fixed plan23 network (prism verified).
// Login EncryptionRequest includes shouldAuthenticate bool true (N5).
// Play BundleDelimiter 0x00 + MultiBlockChange 0x4E with ly<<8|lz<<4|lx (N7).
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
constexpr std::uint8_t CookieResponse = 0x04;
}
namespace sc {
constexpr std::uint8_t Disconnect = 0x00;
constexpr std::uint8_t EncryptionRequest = 0x01;
constexpr std::uint8_t GameProfile = 0x02;        // login success
constexpr std::uint8_t SetCompression = 0x03;
constexpr std::uint8_t CookieRequest = 0x05;
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
constexpr std::uint8_t CookieRequest = 0x00;
constexpr std::uint8_t CustomPayload = 0x01;
constexpr std::uint8_t Disconnect = 0x02;
constexpr std::uint8_t FinishConfiguration = 0x03;
constexpr std::uint8_t KeepAlive = 0x04;
constexpr std::uint8_t Ping = 0x05;
constexpr std::uint8_t ResetChat = 0x06;
constexpr std::uint8_t RegistryData = 0x07;
constexpr std::uint8_t RemoveResourcePack = 0x08;
constexpr std::uint8_t AddResourcePack = 0x09;
constexpr std::uint8_t StoreCookie = 0x0A;
constexpr std::uint8_t Transfer = 0x0B;
constexpr std::uint8_t FeatureFlags = 0x0C;
constexpr std::uint8_t UpdateTags = 0x0D;
constexpr std::uint8_t SelectKnownPacks = 0x0E;
}
}

namespace pl { // play
namespace cs {
constexpr std::uint8_t AcceptTeleportation = 0x00;
constexpr std::uint8_t QueryBlockEntityTag = 0x01;
constexpr std::uint8_t SetDifficulty = 0x03;
constexpr std::uint8_t MessageAck = 0x04;
constexpr std::uint8_t ChatCommand = 0x05;
constexpr std::uint8_t ChatCommandSigned = 0x06;
constexpr std::uint8_t ChatMessage = 0x07;
constexpr std::uint8_t ChatSessionUpdate = 0x08;
constexpr std::uint8_t ChunkBatchReceived = 0x09;
constexpr std::uint8_t ClientCommand = 0x0A;
constexpr std::uint8_t ClientTickEnd = 0x0B;
constexpr std::uint8_t TabComplete = 0x0D;        // command suggestion request
constexpr std::uint8_t EnchantItem = 0x0F;        // enchanting-table click
constexpr std::uint8_t WindowClick = 0x10;        // click container
constexpr std::uint8_t CloseContainer = 0x11;
constexpr std::uint8_t CookieResponse = 0x13;
constexpr std::uint8_t CustomPayload = 0x14;
constexpr std::uint8_t UseEntity = 0x18;
constexpr std::uint8_t KeepAlive = 0x1A;
constexpr std::uint8_t MovePlayerPos = 0x1C;
constexpr std::uint8_t MovePlayerPosRot = 0x1D;
constexpr std::uint8_t MovePlayerRot = 0x1E;
constexpr std::uint8_t MovePlayerStatusOnly = 0x1F;
constexpr std::uint8_t MoveVehicle = 0x20;
constexpr std::uint8_t PingRequest = 0x24;
constexpr std::uint8_t PlaceRecipe = 0x25;        // craft_recipe_request
constexpr std::uint8_t PlayerAction = 0x27;       // block dig
constexpr std::uint8_t EntityAction = 0x28;       // sneak/sprint/...
constexpr std::uint8_t PlayerInput = 0x29;
constexpr std::uint8_t PlayerLoaded = 0x2A;
constexpr std::uint8_t HeldItemSlot = 0x33;
constexpr std::uint8_t SetCreativeModeSlot = 0x36;
constexpr std::uint8_t UpdateSign = 0x39;
constexpr std::uint8_t Spectate = 0x3B;
constexpr std::uint8_t SelectTrade = 0x31;
constexpr std::uint8_t Swing = 0x3A;
constexpr std::uint8_t UseItemOn = 0x3C;
constexpr std::uint8_t UseItem = 0x3D;
}
namespace sc {
constexpr std::uint8_t BundleDelimiter = 0x00;
constexpr std::uint8_t SpawnEntity = 0x01;
constexpr std::uint8_t SpawnExperienceOrb = 0x02;
constexpr std::uint8_t Animation = 0x03;
constexpr std::uint8_t AwardStats = 0x04;
constexpr std::uint8_t AckBlockChange = 0x05;
constexpr std::uint8_t BlockBreakAnimation = 0x06;
constexpr std::uint8_t BlockEntityData = 0x07;
constexpr std::uint8_t BlockAction = 0x08;
constexpr std::uint8_t BlockUpdate = 0x09;
constexpr std::uint8_t BossBar = 0x0A;
constexpr std::uint8_t TradeList = 0x2E;
constexpr std::uint8_t ChangeDifficulty = 0x0B;
constexpr std::uint8_t ChunkBatchFinished = 0x0C;
constexpr std::uint8_t ChunkBatchStart = 0x0D;
constexpr std::uint8_t ClearTitles = 0x0F;
constexpr std::uint8_t SetTitleSubtitle = 0x6A;
constexpr std::uint8_t SetTitleText = 0x6C;
constexpr std::uint8_t SetTitleTime = 0x6D;
constexpr std::uint8_t InitializeWorldBorder = 0x26;
constexpr std::uint8_t WorldBorderLerpSize = 0x53;
constexpr std::uint8_t WorldBorderSize = 0x54;
constexpr std::uint8_t WorldBorderCenter = 0x52;
constexpr std::uint8_t WorldBorderWarningDelay = 0x55;
constexpr std::uint8_t WorldBorderWarningReach = 0x56;
constexpr std::uint8_t ScoreboardDisplayObjective = 0x5C;
constexpr std::uint8_t ScoreboardObjective = 0x64;
constexpr std::uint8_t Teams = 0x67;
constexpr std::uint8_t ResetScore = 0x49; // D26 1.20.3 split: reset_score vs scoreboard_score (Prismarine 0x49 vs 0x68, PR #806)
constexpr std::uint8_t ScoreboardScore = 0x68;
static_assert(ScoreboardDisplayObjective == 0x5C, "Prismarine packet_scoreboard_display_objective 0x5C");
static_assert(ScoreboardObjective == 0x64, "Prismarine packet_scoreboard_objective 0x64");
static_assert(ResetScore == 0x49, "Prismarine packet_reset_score 0x49");
static_assert(ScoreboardScore == 0x68, "Prismarine packet_scoreboard_score 0x68");
constexpr std::uint8_t CommandSuggestions = 0x10; // tab-complete response (packet_tab_complete 0x10)
constexpr std::uint8_t DeclareCommands = 0x11;
constexpr std::uint8_t CloseContainer = 0x12;
constexpr std::uint8_t ContainerSetContent = 0x13;// window items (0x13 per protocol.json, not 0x12)
constexpr std::uint8_t ContainerSetData = 0x14;   // window property (furnace)
constexpr std::uint8_t ContainerSetSlot = 0x15;   // set slot
constexpr std::uint8_t CookieRequest = 0x16;
constexpr std::uint8_t SetCooldown = 0x17;
constexpr std::uint8_t CustomPayload = 0x19;
constexpr std::uint8_t DamageEvent = 0x1A;
constexpr std::uint8_t Disconnect = 0x1D;
constexpr std::uint8_t DisguisedChat = 0x1C;
constexpr std::uint8_t EntityEvent = 0x1F;        // entity status
constexpr std::uint8_t Explosion = 0x21;
constexpr std::uint8_t ForgetLevelChunk = 0x22;
constexpr std::uint8_t GameEvent = 0x23;
constexpr std::uint8_t KeepAlive = 0x27;
constexpr std::uint8_t LevelChunkWithLight = 0x28;
constexpr std::uint8_t WorldEvent = 0x29;
constexpr std::uint8_t WorldParticles = 0x2A;
constexpr std::uint8_t UpdateLight = 0x2B;
constexpr std::uint8_t Login = 0x2C;              // join game
constexpr std::uint8_t MoveEntityPos = 0x2F;
constexpr std::uint8_t MoveEntityPosRot = 0x30;
constexpr std::uint8_t EntityLook = 0x32;
constexpr std::uint8_t OpenScreen = 0x35;
constexpr std::uint8_t PingResponse = 0x38;
constexpr std::uint8_t PlaceGhostRecipe = 0x39;
constexpr std::uint8_t Abilities = 0x3A;
constexpr std::uint8_t PlayerChat = 0x3B;
constexpr std::uint8_t PlayerInfoRemove = 0x3F;
constexpr std::uint8_t PlayerInfoUpdate = 0x40;
constexpr std::uint8_t PlayerPosition = 0x42;     // synchronize position
constexpr std::uint8_t RecipeBookAdd = 0x44;
constexpr std::uint8_t RecipeBookRemove = 0x45;
constexpr std::uint8_t RecipeBookSettings = 0x46;
constexpr std::uint8_t RemoveEntities = 0x47;
constexpr std::uint8_t RemoveMobEffect = 0x48;
constexpr std::uint8_t Respawn = 0x4C;
constexpr std::uint8_t RotateHead = 0x4D;
constexpr std::uint8_t MultiBlockChange = 0x4E;
constexpr std::uint8_t SetCenterChunk = 0x58;
constexpr std::uint8_t SetCursorItem = 0x5A;
constexpr std::uint8_t Camera = 0x57;
constexpr std::uint8_t SetDefaultSpawn = 0x5B;
constexpr std::uint8_t SetEntityMetadata = 0x5D;
constexpr std::uint8_t SetPassengers = 0x65;
constexpr std::uint8_t EntityVelocity = 0x5F;
constexpr std::uint8_t SetEquipment = 0x60;
constexpr std::uint8_t SetExperience = 0x61;
constexpr std::uint8_t SetHealth = 0x62;
constexpr std::uint8_t SetHeldSlot = 0x63;
constexpr std::uint8_t SimulationDistance = 0x69;
constexpr std::uint8_t SoundEffect = 0x6F;
constexpr std::uint8_t StopSound = 0x71;
constexpr std::uint8_t StoreCookie = 0x72;
constexpr std::uint8_t SystemChat = 0x73;
constexpr std::uint8_t Collect = 0x76;
constexpr std::uint8_t EntityTeleport = 0x77;
constexpr std::uint8_t Transfer = 0x7A;
constexpr std::uint8_t UpdateAdvancements = 0x7B;
constexpr std::uint8_t UpdateAttributes = 0x7C;
constexpr std::uint8_t EntityEffect = 0x7D;
constexpr std::uint8_t UpdateRecipes = 0x7E;
constexpr std::uint8_t UpdateTags = 0x7F;
constexpr std::uint8_t UpdateTime = 0x6B;
// Readability aliases used by call sites and the test client.
constexpr std::uint8_t SetTime = UpdateTime;
}
namespace cs {
constexpr std::uint8_t SignUpdate = UpdateSign;          // alias
constexpr std::uint8_t ChangeDifficulty = SetDifficulty; // alias
}
}

} // namespace cppfm::proto
