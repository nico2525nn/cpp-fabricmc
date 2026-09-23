from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise AssertionError(message)
    print(f"PASS {message}")


def between(text, start, end):
    return text.split(start, 1)[1].split(end, 1)[0]


def require_offline_auth_flags(label, command_source):
    require('"--online-mode=false"' in command_source and
            '"--enforce-secure-profile=false"' in command_source,
            f"{label} fake-client launch explicitly disables both authentication checks")


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
    require("secure_chat_policy::classifyMessage(" in chat and
            "secure_chat_policy::isEnforced(" in chat and
            "MessageDisposition::Reject" in chat,
            "chat disposition follows vanilla secure-profile policy and verification")
    status = between(session, "std::string makeStatusJson", "void Session::answerLegacyPing")
    join = between(session, "void Session::sendJoinGame()", "void Session::sendAbilities()")
    require("secure_chat_policy::isEnforced(" in status and
            "secure_chat_policy::isEnforced(" in join,
            "status and Join Game advertise the same effective secure-chat policy")

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

    server_process = (ROOT / "tests/ServerProcess.hpp").read_text()
    options = between(server_process, "struct ServerProcessOptions {", "class ServerProcess {")
    require("bool onlineMode = false;" in options and
            "bool enforceSecureProfile = false;" in options,
            "shared live-server fixture defaults to explicit offline authentication")
    spawn_args = between(server_process, "const char* onlineArg", "_exit(127);")
    require('"--online-mode=true"' in spawn_args and
            '"--enforce-secure-profile=true"' in spawn_args and
            spawn_args.count("onlineArg, secureProfileArg") == 2,
            "shared fixture retains independent explicit true options for online scenarios")
    require_offline_auth_flags("ServerProcess", spawn_args)

    lifecycle = (ROOT / "tests/test_lifecycle_matrix.py").read_text()
    require_offline_auth_flags(
        "lifecycle matrix",
        between(lifecycle, "def _server_command(", "\n\ndef _start_server("))

    goal_live = (ROOT / "tests/test_goal_live_matrix.py").read_text()
    require_offline_auth_flags(
        "goal live matrix",
        between(goal_live, "    def launch(name:", "        server = OwnedServer(command"))

    server_full = (ROOT / "tests/test_server_full.py").read_text()
    full_launcher = between(server_full, "def launch_server(", "\ndef run_owned_command(")
    require_offline_auth_flags("server_full", full_launcher)
    require(full_launcher.index('"--enforce-secure-profile=false"') <
            full_launcher.index("if extra_args:") < full_launcher.index("args += extra_args"),
            "server_full leaves explicit caller arguments last for CLI precedence")

    bot_smoke = (ROOT / "tests/bot_smoke.py").read_text()
    require_offline_auth_flags(
        "bot_smoke",
        between(bot_smoke, 'proc = subprocess.Popen([binary, "--port"',
                "wait_for_server(proc, host, port)"))

    multi_client = (ROOT / "tests/multi_client_test.py").read_text()
    require_offline_auth_flags(
        "multi_client",
        between(multi_client, 'proc = subprocess.Popen([binary, "--port"',
                "wait_for_server(proc, host, port)"))

    stress = (ROOT / "tests/stress_test.py").read_text()
    require_offline_auth_flags(
        "stress_test", between(stress, "cmd = [binary", 'print(f"[stress] starting server'))

    soak = (ROOT / "tests/soak_test.py").read_text()
    require_offline_auth_flags(
        "soak_test", between(soak, "cmd=[binary", 'print(f"[soak] starting server'))

    replay = (ROOT / "tools/replay_vanilla.py").read_text()
    require_offline_auth_flags(
        "replay_vanilla", between(replay, "def launch(", "\ndef wait_ready("))

    soak_bot = (ROOT / "tools/soak_bot.py").read_text()
    require_offline_auth_flags(
        "soak_bot",
        between(soak_bot, "proc = subprocess.Popen(\n                    [", "                    stdout=subprocess.DEVNULL"))

    plan43 = (ROOT / "tests/run_plan43_suite.py").read_text()
    require("from test_server_full import" in plan43 and "launch_server(binary" in plan43,
            "plan43 suite uses server_full's explicitly-offline launch helper")

    print("goal_security_guards: PASS")


if __name__ == "__main__":
    main()
