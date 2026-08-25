// PluginChannels: registry for custom-payload channels (plan2.md,
// "カスタムネットワーキング"). Mirrors Fabric's ServerPlayNetworking /
// ServerConfigurationNetworking shape:
//   * register a receiver per channel (play and/or configuration phase)
//   * send(channel, payload) to any connection
// Unknown inbound channels are delivered to a fallback hook so future
// extensions never break the session.
#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace cppfm::api {

class ChannelRegistry {
public:
    using Payload = std::vector<std::uint8_t>;
    // `phase`: 0 = configuration, 1 = play.
    using Receiver = std::function<void(int phase, const std::string& channel,
                                        const Payload& body)>;

    static ChannelRegistry& get() {
        static ChannelRegistry inst;
        return inst;
    }

    void registerReceiver(const std::string& channel, int phases, Receiver rx) {
        std::lock_guard lk(mtx_);
        Entry& e = channels_[channel];
        e.phases |= phases;
        e.receivers.push_back(std::move(rx));
    }

    // Returns false when nobody listens on that channel/phase.
    bool dispatch(int phase, const std::string& channel, const Payload& body) {
        Entry e;
        {
            std::lock_guard lk(mtx_);
            auto it = channels_.find(channel);
            if (it == channels_.end()) return false;
            if (!(it->second.phases & (1 << phase))) return false;
            e = it->second;                       // copy under lock, invoke outside
        }
        for (auto& rx : e.receivers) rx(phase, channel, body);
        return true;
    }

    std::vector<std::string> knownChannels() const {
        std::lock_guard lk(mtx_);
        std::vector<std::string> out;
        out.reserve(channels_.size());
        for (auto& [k, _] : channels_) out.push_back(k);
        return out;
    }

private:
    struct Entry {
        int phases = 0;                            // bit0 config, bit1 play
        std::vector<Receiver> receivers;
    };
    mutable std::mutex mtx_;
    std::unordered_map<std::string, Entry> channels_;
};

} // namespace cppfm::api
