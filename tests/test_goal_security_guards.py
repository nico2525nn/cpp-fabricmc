from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise AssertionError(message)
    print(f"PASS {message}")


def between(text, start, end):
    return text.split(start, 1)[1].split(end, 1)[0]


def main():
    config = (ROOT / "src/game/ServerConfig.cpp").read_text()
    profile = between(config, "case ConfigKey::SecureProfile:",
                      "case ConfigKey::SecureChat:")
    require("enforcesSecureChat" not in profile,
            "secure profile parser remains independent from secure chat")

    session = (ROOT / "src/game/GameServer_session.cpp").read_text()
    chat = between(session, "void Session::onChatMessage(ReadBuffer& in)",
                   "void Session::onChatCommand(ReadBuffer& in)")
    verified = chat.index("ChatMessageProcessor::verify")
    event = chat.index("srv_.events().chat.fire")
    jvm = chat.index("srv_.jvmRuntime()->onChat")
    require(verified < event < jvm,
            "chat callbacks run only after signature and replay verification")
    require("srv_.config().onlineMode) && !usePlayerChat" in chat,
            "online unverified chat fails closed when enforcement is disabled")

    jvm_source = (ROOT / "src/jvm/JvmRuntime.cpp").read_text()
    set_block = between(jvm_source, "bool JvmRuntime::nativeWorldSetBlock",
                        "std::int64_t JvmRuntime::nativeWorldTime")
    require(set_block.index("gen::blockByState") < set_block.index("runServerMutation"),
            "JNI block mutation validates registry state before enqueueing")
    stop = between(jvm_source, "void JvmRuntime::stop()", "bool JvmRuntime::started()")
    require("blockEventDispatcher().clearLegacyHandlers();" in stop,
            "JVM stop clears legacy callbacks before unload")

    core = (ROOT / "src/game/GameServer_core.cpp").read_text()
    mutation = between(core, "bool GameServer::runOnServerThread",
                       "void GameServer::drainServerThreadTasks")
    require("State::Running" in mutation and "request->waitCv.wait" in mutation,
            "running server mutations wait for a terminal result")

    block_event = (ROOT / "src/game/BlockEvent.hpp").read_text()
    clear = between(block_event, "void clearLegacyHandlers()", "api::EventHook")
    require("placeHandlers_.clear()" in clear and "landHandlers_.clear()" in clear,
            "legacy block handlers have a synchronized unload removal path")

    predicates = (ROOT / "src/game/DatapackManager.hpp").read_text()
    require("predicateRandom01(ctx)" in predicates and "lootingLevel" in predicates,
            "loot predicates use seeded randomness and looting context")

    print("goal_security_guards: PASS")


if __name__ == "__main__":
    main()
