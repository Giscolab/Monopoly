#include "TcpMessageTransport.hpp"

#include "Messaging.hpp"
#include "VoiceChatPacket.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <deque>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace monopoly::messaging
{
    namespace
    {
        using Bytes = std::vector<std::uint8_t>;
        using Clock = std::chrono::steady_clock;
        constexpr std::size_t MaxBody = 1024 * 1024;
        constexpr std::size_t MaxBuffered = 4 * MaxBody;
        constexpr std::size_t MaxPeers = 9; // Host + peers <= ArtLib's 10 receivers.
        constexpr std::uint32_t Magic = 0x31534D4D; // MMS1, little endian.
        constexpr std::size_t HeaderSize = 12;
        enum class Kind : std::uint16_t { Hello = 1, Action = 2, Heartbeat = 3 };

#ifdef _WIN32
        using Socket = SOCKET;
        constexpr Socket InvalidSocket = INVALID_SOCKET;
        void closeSocket(Socket socket) { closesocket(socket); }
        bool pendingError()
        {
            const int error = WSAGetLastError();
            return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
        }
        bool nonblocking(Socket socket)
        {
            u_long enabled = 1;
            return ioctlsocket(socket, FIONBIO, &enabled) == 0;
        }
#else
        using Socket = int;
        constexpr Socket InvalidSocket = -1;
        void closeSocket(Socket socket) { ::close(socket); }
        bool pendingError()
        {
            return errno == EAGAIN || errno == EWOULDBLOCK ||
                errno == EINPROGRESS || errno == EINTR;
        }
        bool nonblocking(Socket socket)
        {
            if (socket >= FD_SETSIZE) return false;
            const int flags = fcntl(socket, F_GETFL, 0);
            if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) != 0)
                return false;
#ifdef SO_NOSIGPIPE
            const int enabled = 1;
            if (setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE,
                    &enabled, sizeof(enabled)) != 0)
                return false;
#endif
            return true;
        }
#endif

        void put(Bytes& bytes, std::uint64_t value, unsigned count)
        {
            for (unsigned index = 0; index < count; ++index)
                bytes.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
        }

        struct Reader
        {
            std::span<const std::uint8_t> bytes;
            std::size_t at{};
            bool valid{true};
            std::uint64_t get(unsigned count)
            {
                if (count > bytes.size() - at)
                {
                    valid = false;
                    return 0;
                }
                std::uint64_t result = 0;
                for (unsigned i = 0; i < count; ++i)
                    result |= std::uint64_t{bytes[at++]} << (i * 8);
                return result;
            }
            bool blob(Bytes& target)
            {
                const auto size = get(4);
                if (!valid || size > bytes.size() - at)
                    return false;
                target.assign(bytes.begin() + at, bytes.begin() + at + size);
                at += static_cast<std::size_t>(size);
                return true;
            }
        };

        Bytes frame(Kind kind, const Bytes& body)
        {
            Bytes bytes;
            bytes.reserve(HeaderSize + body.size());
            put(bytes, Magic, 4);
            put(bytes, 1, 2); // Protocol version.
            put(bytes, static_cast<unsigned>(kind), 2);
            put(bytes, body.size(), 4);
            bytes.insert(bytes.end(), body.begin(), body.end());
            return bytes;
        }

        Bytes encode(const actions::Message& message)
        {
            // No native struct/wchar_t layout crosses the wire. stringA uses
            // 80 UTF-32 code units, with explicit UTF-16 surrogate conversion.
            if (message.binaryData.size() > MaxBody ||
                message.binaryDataA.size() > MaxBody - message.binaryData.size() ||
                message.binaryData.size() + message.binaryDataA.size() > MaxBody - 372)
                return {};
            Bytes body;
            put(body, static_cast<unsigned>(message.action), 2);
            put(body, message.fromPlayer, 1);
            put(body, message.toPlayer, 1);
            for (auto value : {message.numberA, message.numberB, message.numberC,
                               message.numberD, message.numberE})
                put(body, static_cast<std::uint64_t>(value), 8);
            std::array<std::uint32_t, 80> text{};
            std::size_t out = 0;
            for (std::size_t i = 0; i < message.stringA.size() &&
                 message.stringA[i] != 0 && out < text.size() - 1; ++i)
            {
                auto value = static_cast<std::uint32_t>(message.stringA[i]);
                if constexpr (sizeof(wchar_t) == 2)
                {
                    if (value >= 0xD800 && value <= 0xDBFF)
                    {
                        if (++i >= message.stringA.size()) return {};
                        const auto low = static_cast<std::uint32_t>(message.stringA[i]);
                        if (low < 0xDC00 || low > 0xDFFF) return {};
                        value = 0x10000 + ((value - 0xD800) << 10) + low - 0xDC00;
                    }
                }
                if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
                    return {};
                text[out++] = value;
            }
            for (auto value : text) put(body, value, 4);
            for (const auto* blob : {&message.binaryData, &message.binaryDataA})
            {
                put(body, blob->size(), 4);
                body.insert(body.end(), blob->begin(), blob->end());
            }
            return frame(Kind::Action, body);
        }

        bool decode(std::span<const std::uint8_t> body, actions::Message& message)
        {
            Reader reader{body};
            message = {};
            message.action = static_cast<actions::Type>(reader.get(2));
            message.fromPlayer = static_cast<rules::PlayerNumber>(reader.get(1));
            message.toPlayer = static_cast<rules::PlayerNumber>(reader.get(1));
            for (auto* number : {&message.numberA, &message.numberB, &message.numberC,
                                &message.numberD, &message.numberE})
                *number = std::bit_cast<std::int64_t>(reader.get(8));
            std::size_t out = 0;
            bool ended = false;
            for (unsigned i = 0; i < 80; ++i)
            {
                const auto value = static_cast<std::uint32_t>(reader.get(4));
                if (value == 0) { ended = true; continue; }
                if (ended || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
                    return false;
                if constexpr (sizeof(wchar_t) == 2)
                {
                    if (value >= 0x10000)
                    {
                        if (out + 2 >= message.stringA.size()) return false;
                        message.stringA[out++] = static_cast<wchar_t>(0xD800 + ((value - 0x10000) >> 10));
                        message.stringA[out++] = static_cast<wchar_t>(0xDC00 + ((value - 0x10000) & 1023));
                        continue;
                    }
                }
                if (out + 1 >= message.stringA.size()) return false;
                message.stringA[out++] = static_cast<wchar_t>(value);
            }
            return ended && reader.blob(message.binaryData) &&
                reader.blob(message.binaryDataA) && reader.valid && reader.at == body.size();
        }

        struct Peer
        {
            Socket socket{InvalidSocket};
            std::uint32_t id{};
            bool connecting{};
            bool ready{};
            bool gameplay{};
            bool admitted{};
            bool dead{};
            bool readClosed{};
            Bytes input;
            std::deque<Bytes> output;
            std::size_t offset{};
            std::size_t buffered{};
            Clock::time_point lastReceive{Clock::now()};
            Clock::time_point lastSend{Clock::now()};
            Clock::time_point lastProgress{Clock::now()};
        };

        class TcpTransport final : public Transport
        {
        public:
            explicit TcpTransport(bool host, TcpSessionMode mode) : host_(host), mode_(mode) {}
            ~TcpTransport() override
            {
                for (auto& peer : peers_) closeSocket(peer.socket);
                if (listener_ != InvalidSocket) closeSocket(listener_);
#ifdef _WIN32
                if (winsock_) WSACleanup();
#endif
            }

            bool open(std::string_view address, std::uint16_t port)
            {
#ifdef _WIN32
                WSADATA data{};
                if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
                    return fail("WSAStartup failed");
                winsock_ = true;
#endif
                sockaddr_in endpoint{};
                endpoint.sin_family = AF_INET;
                endpoint.sin_port = htons(port);
                if (port == 0 || inet_pton(AF_INET, std::string(address).c_str(), &endpoint.sin_addr) != 1)
                    return fail("TCP endpoint requires a numeric IPv4 address and port 1..65535");
                Socket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (socket == InvalidSocket) return fail("TCP socket creation failed");
                if (!nonblocking(socket))
                {
                    closeSocket(socket);
                    return fail("TCP nonblocking setup failed");
                }
                if (host_)
                {
                    if (::bind(socket, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) != 0 ||
                        ::listen(socket, static_cast<int>(MaxPeers)) != 0)
                    {
                        closeSocket(socket);
                        return fail("TCP bind/listen failed");
                    }
                    listener_ = socket;
                }
                else
                {
                    const bool connecting = ::connect(socket,
                        reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) != 0;
                    if (connecting && !pendingError())
                    {
                        closeSocket(socket);
                        return fail("TCP connect failed");
                    }
                    Peer peer;
                    peer.socket = socket;
                    peer.connecting = connecting;
                    Bytes hello(4, 0);
                    if (mode_ == TcpSessionMode::Gameplay) put(hello, 1, 4);
                    enqueue(peer, frame(Kind::Hello, hello));
                    peers_.push_back(std::move(peer));
                }
                return true;
            }

            bool server() const noexcept override { return host_; }
            bool gameplayEnabled() const noexcept override { return mode_ == TcpSessionMode::Gameplay; }
            std::uint32_t localSourceId() const noexcept override { return localSourceId_; }
            void setPlayerOwner(rules::PlayerNumber player, std::uint32_t source) noexcept override
            {
                if (host_ && player < rules::MaxPlayers) owners_[player] = source;
            }
            bool sourceConnected(std::uint32_t source) const noexcept override
            {
                if (host_ && source == 0) return true;
                return std::any_of(peers_.begin(), peers_.end(), [&](const Peer& peer) {
                    return peer.ready && !peer.dead && (host_ ? peer.id == source : source == 0);
                });
            }
            bool admitSource(std::uint32_t source) override
            {
                if (!host_) return false;
                for (auto& peer : peers_)
                    if (peer.id == source && peer.ready && !peer.dead)
                    {
                        peer.admitted = true;
                        return true;
                    }
                return false;
            }
            bool receiveDisconnectedSource(std::uint32_t& source) override
            {
                if (disconnected_.empty()) return false;
                source = disconnected_.front();
                disconnected_.pop_front();
                return true;
            }
            bool sendToSource(const actions::Message& message, std::uint32_t source) override
            {
                if (!host_ || !validNotification(message)) return false;
                const auto bytes = encode(message);
                if (bytes.empty()) return false;
                for (auto& peer : peers_)
                    if (peer.id == source && peer.ready && peer.admitted && !peer.dead)
                    {
                        if (!peer.gameplay && message.toPlayer != rules::AllPlayers) return false;
                        if (enqueue(peer, bytes)) return true;
                        drop(peer, "TCP peer outgoing queue exceeded its bound");
                        return false;
                    }
                return false;
            }
            bool active() const noexcept override
            {
                if (host_ && gameplayEnabled() && listener_ != InvalidSocket) return true;
                return std::any_of(peers_.begin(), peers_.end(),
                    [](const Peer& peer) { return peer.ready && !peer.dead; });
            }
            std::string_view error() const noexcept override { return error_; }
            std::size_t queued() const noexcept override
            {
                std::size_t count = 0;
                for (const auto& peer : peers_)
                    if (!peer.dead) count = std::max(count, peer.output.size());
                return count;
            }

            bool receive(actions::Message& message) override
            {
                if (incoming_.empty()) return false;
                message = std::move(incoming_.front());
                incoming_.pop_front();
                return true;
            }

            bool send(const actions::Message& message) override
            {
                // Evicting the final slow peer during a RULE batch must not
                // reject subsequent local bank notifications before next pump.
                if (!active()) return host_;
                if (!host_ && !(gameplayEnabled() ? validGameplayAction(message) : validVoice(message))) return false;
                if (host_ && !validNotification(message)) return false;
                // Spectator is not a unique connection. Explicit recipients
                // use sendToSource instead of leaking a private notification.
                if (host_ && message.toPlayer == rules::SpectatorPlayer) return false;
                const Bytes bytes = encode(message);
                if (bytes.empty())
                {
                    if (host_)
                        for (auto& peer : peers_)
                            if (!peer.dead) drop(peer, "TCP action cannot be encoded");
                    return host_;
                }
                bool sent = false;
                for (auto& peer : peers_)
                {
                    if (!peer.ready || peer.dead || (host_ && !peer.admitted)) continue;
                    if (host_ && !peer.gameplay && message.toPlayer != rules::AllPlayers) continue;
                    if (host_ && message.toPlayer < rules::MaxPlayers &&
                        owners_[message.toPlayer] != peer.id) continue;
                    if (!enqueue(peer, bytes))
                        drop(peer, "TCP peer outgoing queue exceeded its bound");
                    else sent = true;
                }
                // A host still delivers locally if the last slow peer was
                // evicted. A client must report failure to its capture owner.
                return host_ || sent;
            }

            void pump() override
            {
                if (listener_ != InvalidSocket && peers_.size() < MaxPeers)
                {
                    // One accept per cycle, so connection floods cannot starve SDL.
                    Socket socket = ::accept(listener_, nullptr, nullptr);
                    if (socket != InvalidSocket)
                    {
                        if (!nonblocking(socket) || nextId_ == 0)
                            closeSocket(socket);
                        else
                        {
                            Peer peer;
                            peer.socket = socket;
                            peer.id = nextId_++;
                            peers_.push_back(std::move(peer));
                        }
                    }
                    else if (!pendingError()) fail("TCP accept failed");
                }

                for (auto& peer : peers_)
                {
                    if (!peer.dead) service(peer);
                    // A synthetic STOP carries the authenticated source of a
                    // departed peer through the same RULE echo path as audio.
                    // Keep the dead peer until there is room for that cleanup.
                    if (peer.dead && host_ && peer.ready && incoming_.size() < MessageQueueCapacity &&
                        disconnected_.size() < MessageQueueCapacity)
                    {
                        actions::Message stop;
                        stop.action = actions::Type::VoiceChat;
                        stop.fromPlayer = rules::SpectatorPlayer;
                        stop.toPlayer = rules::BankPlayer;
                        stop.numberA = rules::AllPlayers;
                        stop.sourceId = peer.id;
                        (void)voicechat::packet::makeStopPacket(stop.binaryDataA);
                        incoming_.push_back(std::move(stop));
                        // Only this socket owner can report a departed source.
                        // MESS expands the identity to all of its player slots.
                        disconnected_.push_back(peer.id);
                        peer.ready = false;
                    }
                }
                std::erase_if(peers_, [&](Peer& peer)
                {
                    if (!peer.dead || (host_ && peer.ready)) return false;
                    closeSocket(peer.socket);
                    return true;
                });
            }

        private:
            bool fail(std::string text) { error_ = std::move(text); return false; }
            void drop(Peer& peer, const char* reason)
            {
                peer.dead = true;
                fail(reason);
                // A disconnected client must not replay old buffered actions.
                if (!host_)
                {
                    incoming_.clear();
                    localSourceId_ = 0;
                }
            }
            bool enqueue(Peer& peer, const Bytes& bytes)
            {
                if (peer.output.size() >= MessageQueueCapacity ||
                    bytes.size() > MaxBuffered - peer.buffered) return false;
                if (peer.output.empty()) peer.lastProgress = Clock::now();
                peer.buffered += bytes.size();
                peer.output.push_back(bytes);
                return true;
            }
            bool validVoice(const actions::Message& message) const
            {
                return message.action == actions::Type::VoiceChat &&
                    message.fromPlayer == rules::SpectatorPlayer &&
                    message.toPlayer == rules::BankPlayer &&
                    message.numberA == rules::AllPlayers && message.numberB == 0 &&
                    message.numberC == 0 && message.numberD == 0 && message.numberE == 0 &&
                    message.stringA[0] == 0 && message.binaryData.empty() &&
                    voicechat::packet::parse(message.binaryDataA, 0) == voicechat::packet::ParseStatus::Ok;
            }

            bool validNotification(const actions::Message& message) const
            {
                const auto action = static_cast<unsigned>(message.action);
                if (message.fromPlayer != rules::BankPlayer || action < 80u || action > 136u)
                    return false;
                if (!gameplayEnabled()) return message.toPlayer == rules::AllPlayers;
                return message.toPlayer == rules::AllPlayers ||
                    message.toPlayer == rules::SpectatorPlayer || message.toPlayer < rules::MaxPlayers;
            }

            bool validGameplayAction(const actions::Message& message) const
            {
                if (message.toPlayer != rules::BankPlayer ||
                    (message.fromPlayer >= rules::MaxPlayers &&
                     message.fromPlayer != rules::SpectatorPlayer)) return false;
                const auto action = static_cast<unsigned>(message.action);
                // Rule.h calls movement/landing internal; never admit bank
                // operations or manufactured disconnect/tick/seed messages.
                if (message.action == actions::Type::DisconnectedPlayer)
                    return message.fromPlayer < rules::MaxPlayers &&
                        message.numberA == message.fromPlayer;
                if (action == 0 || action > 50 || action == 2 ||
                    action == 3 || action == 12 || (action >= 19 && action <= 22))
                    return false;
                if (message.fromPlayer == rules::SpectatorPlayer &&
                    message.action != actions::Type::NamePlayer &&
                    message.action != actions::Type::ResyncClient &&
                    message.action != actions::Type::TextChat &&
                    message.action != actions::Type::VoiceChat) return false;
                if (message.action == actions::Type::VoiceChat)
                    return (message.numberA == rules::AllPlayers ||
                        (message.numberA >= 0 && message.numberA < rules::MaxPlayers)) &&
                        message.numberB == 0 && message.numberC == 0 &&
                        message.numberD == 0 && message.numberE == 0 &&
                        message.stringA[0] == 0 && message.binaryData.empty() &&
                        voicechat::packet::parse(message.binaryDataA, 0) == voicechat::packet::ParseStatus::Ok;
                return true; // Identity/ownership and phase validation belong to MESS/RULE.
            }

            bool consume(Peer& peer, Kind kind, std::span<const std::uint8_t> body)
            {
                if (!peer.ready)
                {
                    if (kind != Kind::Hello || (body.size() != 4 && body.size() != 8)) return false;
                    Reader reader{body};
                    const auto id = static_cast<std::uint32_t>(reader.get(4));
                    const auto capability = body.size() == 8 ? reader.get(4) : 0;
                    if (capability > 1) return false;
                    if (host_)
                    {
                        if (id != 0 || (capability == 1 && !gameplayEnabled())) return false;
                        peer.gameplay = capability == 1;
                        peer.admitted = !gameplayEnabled();
                        Bytes welcome;
                        put(welcome, peer.id, 4);
                        if (body.size() == 8) put(welcome, capability, 4);
                        if (!enqueue(peer, frame(Kind::Hello, welcome))) return false;
                        // Admission causes one explicit resync to all peers.
                        // Existing microphones restart and announce CHAT again.
                        actions::Message resync;
                        resync.action = actions::Type::ResyncClient;
                        resync.fromPlayer = rules::SpectatorPlayer;
                        resync.toPlayer = rules::BankPlayer;
                        resync.numberA = rules::AllPlayers;
                        resync.sourceId = peer.id;
                        incoming_.push_back(std::move(resync));
                    }
                    else
                    {
                        if (id == 0 || (capability == 1) != gameplayEnabled()) return false;
                        peer.id = id;
                        peer.gameplay = capability == 1;
                        localSourceId_ = id;
                    }
                    peer.ready = true;
                    return true;
                }
                if (kind == Kind::Heartbeat) return body.empty();
                if (kind != Kind::Action) return false;
                actions::Message message;
                if (!decode(body, message)) return false;
                if (host_)
                {
                    if (!(peer.gameplay ? validGameplayAction(message) : validVoice(message))) return false;
                    message.sourceId = peer.id;
                }
                else
                {
                    if (!validNotification(message)) return false;
                    if (message.action == actions::Type::NotifyVoiceChat &&
                        (message.numberD < 0 || message.numberD > std::numeric_limits<std::uint32_t>::max() ||
                         voicechat::packet::parse(message.binaryDataA, 0) != voicechat::packet::ParseStatus::Ok))
                        return false;
                }
                incoming_.push_back(std::move(message));
                return true;
            }

            void service(Peer& peer)
            {
                const auto now = Clock::now();
                if (now - peer.lastReceive > std::chrono::seconds(peer.ready ? 30 : 10) ||
                    (!peer.output.empty() && now - peer.lastProgress > std::chrono::seconds(15)))
                {
                    drop(peer, "TCP peer timed out");
                    return;
                }
                if (peer.connecting)
                {
                    fd_set writes, errors;
                    FD_ZERO(&writes); FD_ZERO(&errors);
                    FD_SET(peer.socket, &writes); FD_SET(peer.socket, &errors);
                    timeval timeout{};
#ifdef _WIN32
                    const int selected = ::select(0, nullptr, &writes, &errors, &timeout);
#else
                    const int selected = ::select(peer.socket + 1, nullptr, &writes, &errors, &timeout);
#endif
                    if (selected < 0) { drop(peer, "TCP connection polling failed"); return; }
                    if (selected == 0) return;
                    int status = 0;
#ifdef _WIN32
                    int length = sizeof(status);
#else
                    socklen_t length = sizeof(status);
#endif
                    if (getsockopt(peer.socket, SOL_SOCKET, SO_ERROR,
                            reinterpret_cast<char*>(&status), &length) != 0 || status != 0)
                    { drop(peer, "TCP connection failed"); return; }
                    peer.connecting = false;
                }
                if (peer.ready && peer.output.empty() && now - peer.lastSend > std::chrono::seconds(5))
                    (void)enqueue(peer, frame(Kind::Heartbeat, {}));
                if (!peer.output.empty())
                {
                    const auto& bytes = peer.output.front();
                    int flags = 0;
#ifdef MSG_NOSIGNAL
                    flags = MSG_NOSIGNAL;
#endif
                    const auto sent = ::send(peer.socket,
                        reinterpret_cast<const char*>(bytes.data() + peer.offset),
                        static_cast<int>(std::min<std::size_t>(bytes.size() - peer.offset, 64 * 1024)), flags);
                    if (sent < 0 && !pendingError()) { drop(peer, "TCP send failed"); return; }
                    if (sent > 0)
                    {
                        peer.offset += static_cast<std::size_t>(sent);
                        peer.buffered -= static_cast<std::size_t>(sent);
                        peer.lastSend = peer.lastProgress = now;
                        if (peer.offset == bytes.size()) { peer.output.pop_front(); peer.offset = 0; }
                    }
                }

                if (incoming_.size() >= MessageQueueCapacity) return;
                std::array<std::uint8_t, 64 * 1024> buffer{};
                const auto room = MaxBody + HeaderSize - peer.input.size();
                if (room > 0 && !peer.readClosed)
                {
                    const auto received = ::recv(peer.socket, reinterpret_cast<char*>(buffer.data()),
                        static_cast<int>(std::min(room, buffer.size())), 0);
                    if (received == 0) peer.readClosed = true;
                    if (received < 0 && !pendingError()) { drop(peer, "TCP receive failed"); return; }
                    if (received > 0)
                    {
                        peer.input.insert(peer.input.end(), buffer.begin(), buffer.begin() + received);
                        // Only complete valid frames refresh the timeout below.
                    }
                }
                std::size_t consumed = 0;
                unsigned budget = 16;
                while (budget-- > 0 && incoming_.size() < MessageQueueCapacity &&
                       peer.input.size() - consumed >= HeaderSize)
                {
                    const std::span<const std::uint8_t> input(peer.input);
                    Reader header{input.subspan(consumed, HeaderSize)};
                    if (header.get(4) != Magic || header.get(2) != 1)
                    { drop(peer, "TCP protocol signature/version mismatch"); return; }
                    const auto kind = static_cast<Kind>(header.get(2));
                    const auto size = static_cast<std::size_t>(header.get(4));
                    if (size > MaxBody) { drop(peer, "TCP frame exceeds size limit"); return; }
                    // Admission occurs at MESS's empty-queue boundary. Keep
                    // commands in this bounded per-peer buffer until then;
                    // other peers can still make progress during this pump.
                    if (host_ && gameplayEnabled() && peer.ready &&
                        !peer.admitted && kind == Kind::Action) break;
                    if (peer.input.size() - consumed - HeaderSize < size) break;
                    if (!consume(peer, kind, input.subspan(consumed + HeaderSize, size)))
                    { drop(peer, "TCP frame rejected"); return; }
                    peer.lastReceive = now;
                    consumed += HeaderSize + size;
                }
                peer.input.erase(peer.input.begin(), peer.input.begin() + consumed);
                if (peer.readClosed && (peer.input.empty() ||
                    ((!host_ || !gameplayEnabled() || peer.admitted) && consumed == 0)))
                    drop(peer, "TCP peer disconnected");
            }

            bool host_{};
            TcpSessionMode mode_{TcpSessionMode::VoiceSpectators};
            std::uint32_t localSourceId_{};
            std::array<std::uint32_t, rules::MaxPlayers> owners_{};
#ifdef _WIN32
            bool winsock_{};
#endif
            Socket listener_{InvalidSocket};
            std::uint32_t nextId_{1};
            std::vector<Peer> peers_;
            std::deque<actions::Message> incoming_;
            std::deque<std::uint32_t> disconnected_;
            std::string error_;
        };
    }

    std::expected<std::unique_ptr<Transport>, std::string> openTcpTransport(
        bool host, std::string_view address, std::uint16_t port, TcpSessionMode mode)
    {
        auto transport = std::make_unique<TcpTransport>(host, mode);
        if (!transport->open(address, port))
            return std::unexpected(std::string(transport->error()));
        return std::unique_ptr<Transport>(std::move(transport));
    }
}
