#include "GameServer.hpp"
#include "Session.hpp"
#include "Messages.hpp"
#include "PlayerLoginHandoff.hpp"

#include <array>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppfm {
using namespace proto;

void Session::finalizeSession() {
    if (!registered_) {
        self_->inConfiguration.store(false, std::memory_order_release);
        if (auto* runtime = srv_.jvmRuntime()) {
            try {
                runtime->onPlayerConfigurationAborted(*self_);
            } catch (const std::exception& error) {
                std::fprintf(stderr,
                    "[cppfm] session configuration-abort hook failed: %s\n",
                    error.what());
            } catch (...) {
                std::fprintf(stderr,
                    "[cppfm] session configuration-abort hook failed\n");
            }
        }
        return;
    }

    // Cleanup runs after the main session loop and must not leave a stale
    // player in the global roster when a hook or persistence operation fails.
    // Each step is isolated so later cleanup still runs. Replacements wait
    // for this result before publishing the new player.
    registered_ = false;
    PlayerHandoffReadiness handoffReadiness;
    const std::string nameHandoffKey = "name:" + self_->name;
    const auto cleanupStep = [this, &handoffReadiness](
        const char* name, HandoffCleanupPolicy policy, auto&& step) {
        runPlayerHandoffCleanupStep(
            name, policy, handoffReadiness,
            std::forward<decltype(step)>(step),
            [name](std::string_view, const std::exception* error) {
                if (error) {
                    std::fprintf(stderr,
                        "[cppfm] session cleanup (%s) failed: %s\n",
                        name, error->what());
                } else {
                    std::fprintf(stderr,
                        "[cppfm] session cleanup (%s) failed\n", name);
                }
            });
    };
    constexpr auto kRequired = HandoffCleanupPolicy::RequiredForReplacement;
    constexpr auto kBestEffort = HandoffCleanupPolicy::BestEffort;
    const auto fenceFailedIdentity = [this, &nameHandoffKey] {
        const std::array<std::string, 2> identityKeys{
            GameServer::uuidToHex(self_->uuid), nameHandoffKey};
        for (const auto& identityKey : identityKeys) {
            try {
                srv_.blockPlayerLoginHandoff(identityKey);
            } catch (...) {
                std::fprintf(stderr,
                    "[cppfm] could not fence failed player handoff for %s\n",
                    self_->name.c_str());
            }
        }
    };

    cleanupStep("session registry removal", kRequired, [this] {
        srv_.unregisterSession(this);
    });
    cleanupStep("JVM player-handle retirement", kRequired, [this] {
        if (auto* runtime = srv_.jvmRuntime())
            runtime->beginPlayerRetirement(*self_);
    });
    cleanupStep("inactive transition", kRequired, [this] {
        // Stop simulation and retire visible entity state before callbacks or
        // disk writes can delay cleanup.
        srv_.removePlayerTrackingFor(*self_);
    });
    cleanupStep("JVM quit hook", kBestEffort, [this] {
        if (srv_.jvmRuntime()) srv_.jvmRuntime()->onPlayerQuit(*self_);
    });
    cleanupStep("player quit event", kBestEffort, [this] {
        api::PlayerQuitEvent event;
        event.player = self_.get();
        srv_.events().quit.fire(event);
    });
    cleanupStep("progress save", kRequired, [this, &handoffReadiness] {
        if (!srv_.savePlayerProgress(*self_))
            handoffReadiness.recordFailure(kRequired);
    });
    cleanupStep("leave message", kBestEffort, [this] {
        srv_.broadcastSystemText(msg::kYellow + self_->name + " left the game",
                                 nullptr);
    });
    cleanupStep("player list removal", kRequired, [this] {
        srv_.serializePlayerTrackingPublication([this] {
            // A duplicate login may already have replaced this session in the
            // roster. Do not remove that replacement's published entry.
            for (const auto& current : srv_.playersSnapshot()) {
                if (!current || current.get() == self_.get()) continue;
                std::lock_guard playerLock(current->stateMtx);
                if (current->inPlay && current->uuid == self_->uuid) return;
            }
            WriteBuffer remove;
            remove.varint(1);
            remove.uuid(self_->uuid.data());
            srv_.broadcastPacketExcept(nullptr, pl::sc::PlayerInfoRemove, remove);
        });
    });
    cleanupStep("chat suggestions", kBestEffort, [this] {
        srv_.broadcastChatSuggestions(1, std::vector<std::string>{self_->name},
                                      nullptr);
    });
    cleanupStep("score reset", kBestEffort, [this] {
        const auto affected = srv_.scoreboard.resetAllScores(self_->name);
        if (!affected.empty()) srv_.sendResetScoreAllWildcard(self_->name);
    });
    cleanupStep("player save", kRequired, [this, &handoffReadiness] {
        if (preservePlayerDataOnDisconnect_ ||
            !srv_.savePlayerData(GameServer::uuidToHex(self_->uuid), *self_))
            handoffReadiness.recordFailure(kRequired);
    });

    // Publish the failure fence before removing the only roster record that
    // carries this cleanup future, avoiding a replacement race.
    if (!handoffReadiness.ready()) fenceFailedIdentity();
    cleanupStep("roster removal", kRequired, [this] {
        srv_.removePlayer(self_.get());
    });
    if (!handoffReadiness.ready()) fenceFailedIdentity();
    try {
        cleanupCompletionPromise_.set_value(handoffReadiness.ready());
    } catch (...) {
        std::fprintf(stderr,
            "[cppfm] could not publish cleanup completion for %s\n",
            self_->name.c_str());
    }
}

} // namespace cppfm
