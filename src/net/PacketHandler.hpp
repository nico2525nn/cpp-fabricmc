// PacketHandler: IPacketHandler interface + registry for dispatching packets.
// Mirrors Netty ChannelPipeline handler concept: decodes then dispatches to handler.
#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <stdexcept>
#include "../core/ByteBuffer.hpp"

namespace cppfm {

class Connection; // forward
struct Player;    // forward

// Packet as seen by handlers: id + payload bytes (excluding framing).
struct Packet {
    std::uint8_t id = 0;
    std::vector<std::uint8_t> payload;

    Packet() = default;
    Packet(std::uint8_t i, std::vector<std::uint8_t> p) : id(i), payload(std::move(p)) {}
    Packet(std::uint8_t i, const std::uint8_t* d, std::size_t n) : id(i), payload(d, d + n) {}

    ReadBuffer reader() const { return ReadBuffer(payload.data(), payload.size()); }
    ReadBuffer readerWithId(std::vector<std::uint8_t>& tmp) const {
        tmp.clear();
        tmp.reserve(1 + payload.size());
        tmp.push_back(id);
        tmp.insert(tmp.end(), payload.begin(), payload.end());
        return ReadBuffer(tmp.data(), tmp.size());
    }
};

// IPacketHandler: per-packet handler interface.
// Implement handle(packet, connection) for each packet type.
class IPacketHandler {
public:
    virtual ~IPacketHandler() = default;
    virtual void handle(const Packet& packet, Connection& connection) = 0;
    // Optional overload with player context (default forwards)
    virtual void handle(const Packet& packet, Connection& connection, Player* /*player*/) {
        handle(packet, connection);
    }
};

// Functional handler for lambdas
class FuncPacketHandler : public IPacketHandler {
public:
    using Fn = std::function<void(const Packet&, Connection&)>;
    explicit FuncPacketHandler(Fn f) : fn_(std::move(f)) {}
    void handle(const Packet& p, Connection& c) override { fn_(p, c); }
private:
    Fn fn_;
};

// Registry mapping packet id -> handler
class PacketHandlerRegistry {
public:
    void registerHandler(std::uint8_t id, std::unique_ptr<IPacketHandler> h) {
        handlers_[id] = std::move(h);
    }
    void registerHandler(std::uint8_t id, IPacketHandler* raw) {
        handlers_[id].reset(raw);
    }
    void registerHandler(std::uint8_t id, typename FuncPacketHandler::Fn fn) {
        handlers_[id] = std::make_unique<FuncPacketHandler>(std::move(fn));
    }

    IPacketHandler* getHandler(std::uint8_t id) const {
        auto it = handlers_.find(id);
        return it == handlers_.end() ? nullptr : it->second.get();
    }

    bool hasHandler(std::uint8_t id) const { return getHandler(id) != nullptr; }

    // Dispatch: returns true if handler found and invoked.
    bool dispatch(const Packet& packet, Connection& conn) const {
        auto* h = getHandler(packet.id);
        if (!h) return false;
        h->handle(packet, conn);
        return true;
    }

    bool dispatch(std::uint8_t id, const std::vector<std::uint8_t>& payload, Connection& conn) const {
        Packet p{id, payload};
        return dispatch(p, conn);
    }

    bool dispatch(std::uint8_t id, const WriteBuffer& payloadBuf, Connection& conn) const {
        Packet p{id, payloadBuf.data};
        return dispatch(p, conn);
    }

    // Dispatch with ReadBuffer view (avoids copy)
    bool dispatch(const std::vector<std::uint8_t>& body, Connection& conn) const {
        if (body.empty()) return false;
        Packet p;
        p.id = body[0];
        if (body.size() > 1) p.payload.assign(body.begin() + 1, body.end());
        return dispatch(p, conn);
    }

    void clear() { handlers_.clear(); }
    std::size_t size() const { return handlers_.size(); }

private:
    std::unordered_map<std::uint8_t, std::unique_ptr<IPacketHandler>> handlers_;
};

// Global registry singleton (optional)
inline PacketHandlerRegistry& globalPacketHandlers() {
    static PacketHandlerRegistry r;
    return r;
}

} // namespace cppfm
