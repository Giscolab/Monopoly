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
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using namespace monopoly;
    using Bytes = std::vector<std::uint8_t>;
    using Transport = messaging::Transport;

    void require(bool condition, std::string_view description)
    {
        if (!condition) throw std::runtime_error(std::string(description));
    }

    template<class Predicate, class Pump>
    void until(Predicate done, Pump pump, std::string_view description)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!done())
        {
            require(std::chrono::steady_clock::now() < deadline, description);
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

#ifdef _WIN32
    using Socket = SOCKET;
    constexpr Socket InvalidSocket = INVALID_SOCKET;
    void closeSocket(Socket socket) { closesocket(socket); }
    bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
    using Socket = int;
    constexpr Socket InvalidSocket = -1;
    void closeSocket(Socket socket) { ::close(socket); }
    bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
#endif

    struct SocketRuntime
    {
        SocketRuntime()
        {
#ifdef _WIN32
            WSADATA data{};
            require(WSAStartup(MAKEWORD(2, 2), &data) == 0, "Winsock test initialization");
#endif
        }
        ~SocketRuntime()
        {
#ifdef _WIN32
            WSACleanup();
#endif
        }
    };

    struct SocketOwner
    {
        Socket socket{::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
        SocketOwner() { require(socket != InvalidSocket, "create real TCP fixture socket"); }
        ~SocketOwner() { close(); }
        SocketOwner(const SocketOwner&) = delete;
        SocketOwner& operator=(const SocketOwner&) = delete;
        void close()
        {
            if (socket != InvalidSocket) closeSocket(socket);
            socket = InvalidSocket;
        }
    };

    sockaddr_in endpoint(std::uint16_t port)
    {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return address;
    }

    std::uint16_t unusedPort()
    {
        SocketOwner probe;
        auto address = endpoint(0);
        require(::bind(probe.socket, reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) == 0, "reserve an ephemeral loopback port");
#ifdef _WIN32
        int length = sizeof(address);
#else
        socklen_t length = sizeof(address);
#endif
        require(getsockname(probe.socket, reinterpret_cast<sockaddr*>(&address),
            &length) == 0, "read ephemeral loopback port");
        return ntohs(address.sin_port);
    }

    struct Host
    {
        std::uint16_t port{};
        std::unique_ptr<Transport> transport;
        Host()
        {
            // Port zero is deliberately forbidden by the public API. Retry
            // reservation races without assuming an available fixed port.
            for (unsigned attempt = 0; attempt < 8 && !transport; ++attempt)
            {
                port = unusedPort();
                auto opened = messaging::openTcpTransport(true, "127.0.0.1", port);
                if (opened) transport = std::move(*opened);
            }
            require(transport != nullptr, "open real loopback host");
        }
        std::unique_ptr<Transport> client() const
        {
            auto opened = messaging::openTcpTransport(false, "127.0.0.1", port);
            require(opened.has_value(), "open real loopback client");
            return std::move(*opened);
        }
    };

    struct RawClient : SocketOwner
    {
        explicit RawClient(std::uint16_t port)
        {
            const auto address = endpoint(port);
            require(::connect(socket, reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)) == 0, "connect raw loopback fixture");
#ifdef _WIN32
            u_long enabled = 1;
            require(ioctlsocket(socket, FIONBIO, &enabled) == 0, "nonblocking raw fixture");
#else
            const int flags = fcntl(socket, F_GETFL, 0);
            require(flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0,
                "nonblocking raw fixture");
#endif
        }
        void write(std::span<const std::uint8_t> bytes, Transport& host)
        {
            std::size_t sent = 0;
            until([&] { return sent == bytes.size(); }, [&]
            {
                int flags = 0;
#ifdef MSG_NOSIGNAL
                flags = MSG_NOSIGNAL;
#endif
                const auto count = ::send(socket,
                    reinterpret_cast<const char*>(bytes.data() + sent),
                    static_cast<int>(bytes.size() - sent), flags);
                require(count >= 0 || wouldBlock(), "raw fixture send succeeds");
                if (count > 0) sent += static_cast<std::size_t>(count);
                host.pump();
            }, "raw fixture send deadline");
        }
        void finishWriting()
        {
            // Closing with the unread server Hello may generate a Windows
            // reset. Use an orderly EOF to test delivery of buffered frames.
#ifdef _WIN32
            constexpr int direction = SD_SEND;
#else
            constexpr int direction = SHUT_WR;
#endif
            require(::shutdown(socket, direction) == 0, "send orderly fixture EOF");
        }
    };

    constexpr std::array<std::uint8_t, 16> Hello{
        'M', 'M', 'S', '1', 1, 0, 1, 0, 4, 0, 0, 0, 0, 0, 0, 0};
    const Bytes Stop{'S', 'T', 'O', 'P', 0, 0, 0, 0};

    actions::Message voice(const Bytes& packet = Stop)
    {
        actions::Message message;
        message.action = actions::Type::VoiceChat;
        message.fromPlayer = rules::SpectatorPlayer;
        message.toPlayer = rules::BankPlayer;
        message.numberA = rules::AllPlayers;
        message.binaryDataA = packet;
        return message;
    }

    actions::Message notification(const Bytes& packet = Stop)
    {
        auto message = voice(packet);
        message.action = actions::Type::NotifyVoiceChat;
        message.fromPlayer = rules::BankPlayer;
        message.toPlayer = rules::AllPlayers;
        return message;
    }

    // Independent wire fixture from NETWORK_VOICE.md: 12-byte header,
    // 372-byte fixed action body, then the second blob. No production encoder.
    Bytes wireVoice(std::uint8_t serial = 0)
    {
        Bytes bytes(12 + 372 + 9, 0);
        const std::array<std::uint8_t, 12> header{
            'M', 'M', 'S', '1', 1, 0, 2, 0, 0x7D, 1, 0, 0};
        std::copy(header.begin(), header.end(), bytes.begin());
        bytes[12] = 38; // VoiceChat.
        bytes[14] = 9;  // Spectator.
        bytes[15] = 6;  // Bank.
        bytes[16] = 7;  // AllPlayers.
        bytes[380] = 9; // binaryDataA length.
        const std::array<std::uint8_t, 9> data{'D', 'A', 'T', 'N', 1, 0, 0, 0, serial};
        std::copy(data.begin(), data.end(), bytes.begin() + 384);
        return bytes;
    }

    std::vector<actions::Message> drain(Transport& transport)
    {
        std::vector<actions::Message> messages;
        actions::Message message;
        while (transport.receive(message)) messages.push_back(std::move(message));
        return messages;
    }

    std::uint32_t admit(Host& host, Transport& client)
    {
        require(!client.active(), "connecting client is inactive before handshake");
        until([&] { return client.active() && host.transport->active(); }, [&]
        {
            host.transport->pump();
            client.pump();
        }, "real client handshake deadline");
        const auto messages = drain(*host.transport);
        require(messages.size() == 1 && messages[0].action == actions::Type::ResyncClient &&
            messages[0].numberA == rules::AllPlayers && messages[0].sourceId != 0,
            "admission emits exactly one all-player resync with nonzero source");
        return messages[0].sourceId;
    }

    std::uint32_t admit(Host& host, RawClient& client)
    {
        client.write(Hello, *host.transport);
        until([&] { return host.transport->active(); }, [&] { host.transport->pump(); },
            "raw handshake deadline");
        const auto messages = drain(*host.transport);
        require(messages.size() == 1 && messages[0].action == actions::Type::ResyncClient,
            "raw handshake emits one admission");
        return messages[0].sourceId;
    }

    void testEndpointAndAdmission()
    {
        require(!messaging::openTcpTransport(true, "localhost", 12345),
            "DNS names are rejected by numeric-only endpoint contract");
        require(!messaging::openTcpTransport(true, "127.0.0.1", 0), "port zero is rejected");
        Host host;
        require(host.transport->server() && !host.transport->active(),
            "listener alone does not activate a network session");
        auto first = host.client();
        require(!first->server(), "client role is set before handshake");
        const auto firstId = admit(host, *first);
        auto second = host.client();
        const auto secondId = admit(host, *second);
        require(secondId != firstId, "late join gets a distinct source identity");
        first.reset();
        std::vector<actions::Message> departed;
        until([&] { return !departed.empty(); }, [&]
        {
            host.transport->pump();
            second->pump();
            departed = drain(*host.transport);
        }, "abrupt disconnect STOP deadline");
        require(departed.size() == 1 && departed[0].action == actions::Type::VoiceChat &&
            departed[0].sourceId == firstId && departed[0].binaryDataA == Stop &&
            host.transport->active(), "disconnect sends one exact STOP while the other peer survives");
        auto third = host.client();
        const auto thirdId = admit(host, *third);
        require(thirdId != firstId && thirdId != secondId, "departed identities are never reused");
    }

    void testPartialFramesAndIncomingBound()
    {
        Host host;
        RawClient client(host.port);
        client.write(std::span(Hello).first(5), *host.transport);
        require(!host.transport->active() && drain(*host.transport).empty(),
            "partial handshake cannot admit a peer");
        client.write(std::span(Hello).subspan(5), *host.transport);
        until([&] { return host.transport->active(); }, [&] { host.transport->pump(); },
            "split hello completion deadline");
        const auto admitted = drain(*host.transport);
        require(admitted.size() == 1, "split hello admits exactly once");
        const auto id = admitted[0].sourceId;
        const auto frame = wireVoice(0xA5);
        client.write(std::span(frame).first(11), *host.transport);
        require(drain(*host.transport).empty(), "partial header dispatches no action");
        client.write(std::span(frame).subspan(11, frame.size() - 12), *host.transport);
        require(drain(*host.transport).empty(), "partial body dispatches no action");
        client.write(std::span(frame).last(1), *host.transport);
        std::vector<actions::Message> received;
        until([&] { return !received.empty(); }, [&]
        {
            host.transport->pump();
            received = drain(*host.transport);
        }, "split action completion deadline");
        require(received.size() == 1 && received[0].sourceId == id &&
            received[0].binaryDataA.back() == 0xA5, "split action is delivered exactly once with its payload");

        Bytes burst;
        constexpr auto Count = messaging::MessageQueueCapacity + 5;
        for (std::size_t i = 0; i < Count; ++i)
        {
            const auto next = wireVoice(static_cast<std::uint8_t>(i));
            burst.insert(burst.end(), next.begin(), next.end());
        }
        client.write(burst, *host.transport);
        for (unsigned i = 0; i < 30; ++i) host.transport->pump();
        received = drain(*host.transport);
        require(received.size() == messaging::MessageQueueCapacity,
            "coalesced input stops at the public incoming queue capacity");
        client.finishWriting();
        until([&] { return received.size() == Count + 1; }, [&]
        {
            host.transport->pump();
            auto batch = drain(*host.transport);
            received.insert(received.end(), batch.begin(), batch.end());
        }, "backpressured input and final STOP deadline");
        for (std::size_t i = 0; i < Count; ++i)
            require(received[i].sourceId == id && received[i].binaryDataA ==
                Bytes({'D', 'A', 'T', 'N', 1, 0, 0, 0, static_cast<std::uint8_t>(i)}),
                "backpressure preserves every data frame in order across disconnect");
        require(received.back().sourceId == id && received.back().binaryDataA == Stop &&
            !host.transport->active(), "deferred disconnect STOP follows all complete frames");
    }

    void testRejectedWireFrames()
    {
        for (unsigned scenario = 0; scenario < 7; ++scenario)
        {
            Host host;
            RawClient client(host.port);
            const auto id = admit(host, client);
            auto bad = wireVoice();
            switch (scenario)
            {
            case 0: bad[0] = 'X'; break; // Signature.
            case 1: bad[4] = 2; break; // Version.
            case 2: bad[8] = 1; bad[9] = 0; bad[10] = 0x10; break; // > 1 MiB.
            case 3: bad[12] = 14; break; // RollDice from a voice-only spectator.
            case 4: bad[40] = 99; break; // Spoofed numberD source field.
            case 5: bad[384 + 4] = 2; break; // Truncated ArtLib chunk.
            case 6: bad[56] = 0; bad[57] = 0xD8; break; // UTF-32 surrogate.
            }
            client.write(bad, *host.transport);
            until([&] { return !host.transport->active(); }, [&] { host.transport->pump(); },
                "malformed frame rejection deadline");
            const auto received = drain(*host.transport);
            require(received.size() == 1 && received[0].action == actions::Type::VoiceChat &&
                received[0].sourceId == id && received[0].binaryDataA == Stop &&
                !host.transport->error().empty(),
                "invalid frame is rejected before RULE, with only connection STOP delivered");
        }
    }

    void testOutgoingBounds()
    {
        Host host;
        auto client = host.client();
        (void)admit(host, *client);
        // Do not pump: this proves the transport queue bound independently of
        // the machine's TCP send buffer size or scheduler speed.
        for (std::size_t i = 0; i < messaging::MessageQueueCapacity; ++i)
            require(client->send(voice()), "valid voice fits up to the outgoing frame limit");
        require(client->queued() == messaging::MessageQueueCapacity,
            "queue accounting includes every unpumped outgoing frame");
        require(!client->send(voice()) && !client->active() && !client->error().empty(),
            "overflow disconnects the sender and reports failure instead of dropping STOP silently");

        Host byteHost;
        auto receiver = byteHost.client();
        (void)admit(byteHost, *receiver);
        auto large = notification();
        large.action = actions::Type::NotifyClientResyncInfo;
        large.binaryDataA.assign(1024 * 1024 - 372, 0x5A);
        for (unsigned i = 0; i < 3; ++i)
            require(byteHost.transport->send(large), "maximum body fits the byte queue limit");
        require(byteHost.transport->active() && byteHost.transport->queued() == 3,
            "three maximum-sized frames remain queued");
        require(byteHost.transport->send(large) && !byteHost.transport->active(),
            "four MiB bound counts headers and evicts the slow peer while preserving host delivery");
        require(byteHost.transport->send(notification()),
            "later host notifications still succeed after the final peer eviction");
    }

    void testNotificationRoundTrip()
    {
        Host host;
        auto client = host.client();
        (void)admit(host, *client);
        actions::Message sent;
        sent.action = actions::Type::NotifyTextChat;
        sent.fromPlayer = rules::BankPlayer;
        sent.toPlayer = rules::AllPlayers;
        sent.numberA = std::numeric_limits<std::int64_t>::min();
        sent.numberB = std::numeric_limits<std::int64_t>::max();
        sent.numberC = -1234567890123LL;
        sent.numberD = 0x123456789ABCDEFLL;
        sent.numberE = -1;
        constexpr std::wstring_view Text = L"caf\u00E9 \U0001F680";
        std::copy(Text.begin(), Text.end(), sent.stringA.begin());
        sent.binaryData.resize(80000);
        sent.binaryDataA.resize(100000);
        for (std::size_t i = 0; i < sent.binaryData.size(); ++i)
            sent.binaryData[i] = static_cast<std::uint8_t>(i % 251);
        for (std::size_t i = 0; i < sent.binaryDataA.size(); ++i)
            sent.binaryDataA[i] = static_cast<std::uint8_t>(250 - i % 251);
        sent.sourceId = 999;
        require(host.transport->send(sent), "send multi-read notification");
        std::vector<actions::Message> received;
        until([&] { return !received.empty(); }, [&]
        {
            host.transport->pump();
            client->pump();
            received = drain(*client);
        }, "multi-read notification deadline");
        require(received.size() == 1 && received[0].action == sent.action &&
            received[0].fromPlayer == sent.fromPlayer && received[0].toPlayer == sent.toPlayer &&
            received[0].numberA == sent.numberA && received[0].numberB == sent.numberB &&
            received[0].numberC == sent.numberC && received[0].numberD == sent.numberD &&
            received[0].numberE == sent.numberE && received[0].stringA == sent.stringA &&
            received[0].binaryData == sent.binaryData && received[0].binaryDataA == sent.binaryDataA &&
            received[0].sourceId == 0,
            "split TCP I/O preserves signed i64, Unicode, separate blobs and strips ingress metadata");
    }

    struct MessagingSession
    {
        MessagingSession() { require(messaging::initialize(), "initialize real Messaging"); }
        ~MessagingSession() { messaging::shutdown(); }
    };

    void testAdmissionHeadroom()
    {
        MessagingSession session;
        Host host;
        auto first = host.client();
        require(messaging::startNetwork(std::move(host.transport)), "MESS owns admission fixture host");
        until([&] { return first->active() && messaging::networkMode(); }, [&]
        {
            messaging::pumpNetwork();
            first->pump();
        }, "first admission fixture handshake deadline");
        actions::Message message;
        require(messaging::receiveAction(message) && message.action == actions::Type::ResyncClient,
            "first admission is delivered before filling MESS");
        for (std::size_t i = 0; i < messaging::MessageQueueCapacity - 1; ++i)
            require(messaging::sendAction(actions::Type::Tick, rules::BankPlayer,
                rules::BankPlayer, static_cast<std::int64_t>(i)), "fill MESS with ordered old work");
        require(messaging::sendAction(voice()), "last MESS slot holds prior local voice");
        auto late = host.client();
        until([&] { return late->active(); }, [&]
        {
            messaging::pumpNetwork();
            first->pump();
            late->pump();
        }, "late admission while MESS is full deadline");
        require(late->send(voice()), "late peer sends voice behind admission");
        require(messaging::receiveVoiceChatOnly(message) && message.sourceId == 0 &&
            message.binaryDataA == Stop, "animation lock still extracts the prior local voice");
        for (unsigned i = 0; i < 5; ++i)
        {
            late->pump();
            messaging::pumpNetwork();
            require(messaging::currentQueueSize() == messaging::MessageQueueCapacity - 1,
                "ordinary pumping holds later remote voice outside the resync response queue");
        }
        for (std::size_t i = 0; i < messaging::MessageQueueCapacity - 1; ++i)
            require(messaging::receiveAction(message) && message.action == actions::Type::Tick &&
                message.numberA == static_cast<std::int64_t>(i),
                "pending admission preserves all preexisting work in order");
        require(messaging::receiveAction(message) && message.action == actions::Type::ResyncClient &&
            message.sourceId != 0 && messaging::currentQueueSize() == 0,
            "late admission reaches RULE with the complete MESS queue available");
        const auto source = message.sourceId;
        for (std::size_t i = 0; i < messaging::MessageQueueCapacity; ++i)
            require(messaging::sendAction(actions::Type::Tick, rules::BankPlayer,
                rules::BankPlayer), "all queue slots remain available for the synchronous resync response");
        messaging::clearActionQueue();
        bool found = false;
        until([&] { return found; }, [&]
        {
            late->pump();
            found = messaging::receiveVoiceChatOnly(message);
        }, "voice following admitted resync deadline");
        require(message.sourceId == source && message.binaryDataA == Stop,
            "held remote voice is delivered with the admitted source after resync");
    }

    void testEstablishedVoiceDuringPendingAdmission()
    {
        MessagingSession session;
        Host host;
        auto established = host.client();
        require(messaging::startNetwork(std::move(host.transport)), "MESS owns locked-admission fixture host");
        until([&] { return established->active() && messaging::networkMode(); }, [&]
        {
            messaging::pumpNetwork();
            established->pump();
        }, "established voice peer handshake deadline");
        actions::Message message;
        require(messaging::receiveAction(message) && message.action == actions::Type::ResyncClient,
            "established speaker is admitted before animation lock");
        const auto source = message.sourceId;
        Bytes chat;
        const voicechat::packet::WaveFormat pcm{1, 1, 11025, 11025, 1, 8, 0};
        require(voicechat::packet::makeStartPacket(pcm, {}, 0, 100, chat) &&
            established->send(voice(chat)), "established speaker announces CHAT");
        bool receivedChat = false;
        until([&] { return receivedChat; }, [&]
        {
            established->pump();
            receivedChat = messaging::receiveVoiceChatOnly(message);
        }, "established speaker CHAT ingress deadline");
        require(message.action == actions::Type::VoiceChat && message.sourceId == source &&
            message.binaryDataA == chat, "established CHAT reaches voice path before late admission");

        require(messaging::sendAction(actions::Type::Tick, rules::BankPlayer, rules::BankPlayer, 123),
            "animation lock retains an ordinary action");
        auto late = host.client();
        until([&] { return late->active(); }, [&]
        {
            established->pump();
            late->pump();
            require(!messaging::receiveVoiceChatOnly(message),
                "voice-only receiver never delivers admission during the lock");
        }, "late handshake under animation lock deadline");
        require(messaging::currentQueueSize() == 1 && messaging::queuedActionCount() == 2,
            "late admission is pending separately from blocked ordinary work");
        const Bytes data{'D', 'A', 'T', 'N', 4, 0, 0, 0, 128, 140, 116, 128};
        require(established->send(voice(data)) && established->send(voice()),
            "established speaker sends DATN then STOP behind late admission");
        std::vector<actions::Message> liveVoice;
        until([&] { return liveVoice.size() == 2; }, [&]
        {
            established->pump();
            late->pump();
            if (messaging::receiveVoiceChatOnly(message))
            {
                require(message.action == actions::Type::VoiceChat,
                    "only voice can cross the active animation lock");
                liveVoice.push_back(message);
            }
        }, "established DATN and STOP must flow while late admission is pending");
        require(liveVoice[0].sourceId == source && liveVoice[0].binaryDataA == data &&
            liveVoice[1].sourceId == source && liveVoice[1].binaryDataA == Stop,
            "pending admission preserves established voice payload, identity and DATN-before-STOP order");
        require(!messaging::receiveVoiceChatOnly(message) && messaging::currentQueueSize() == 1,
            "voice-only reads leave admission and ordinary work pending after speaker STOP");
        require(messaging::receiveAction(message) && message.action == actions::Type::Tick &&
            message.numberA == 123, "unlock first delivers the preexisting ordinary action");
        require(messaging::receiveAction(message) && message.action == actions::Type::ResyncClient &&
            message.sourceId != source && message.sourceId != 0 && messaging::currentQueueSize() == 0,
            "unlock delivers late admission with full MESS headroom after uninterrupted voice");
        for (std::size_t i = 0; i < messaging::MessageQueueCapacity; ++i)
            require(messaging::sendAction(actions::Type::Tick, rules::BankPlayer, rules::BankPlayer),
                "all synchronous resync response slots remain usable after voice bypass");
    }

    void testMessagingIngressAndClientLoss()
    {
        {
            MessagingSession session;
            Host host;
            auto client = host.client();
            require(messaging::startNetwork(std::move(host.transport)), "Messaging owns the real host");
            require(messaging::serverMode() && !messaging::networkMode(),
                "host role and network activity are separate before handshake");
            until([&] { return client->active() && messaging::networkMode(); }, [&]
            {
                messaging::pumpNetwork();
                client->pump();
            }, "Messaging host handshake deadline");
            actions::Message admitted;
            require(messaging::receiveAction(admitted) &&
                admitted.action == actions::Type::ResyncClient, "MESS forwards real admission to RULE");
            auto remote = voice();
            remote.sourceId = 0xDEADBEEF;
            require(client->send(remote), "local sourceId metadata cannot prevent valid voice transmission");
            require(messaging::sendAction(actions::Type::Tick, rules::BankPlayer,
                rules::BankPlayer), "queue a normal action behind the animation gate");
            actions::Message delivered;
            bool found = false;
            until([&] { return found; }, [&]
            {
                client->pump();
                found = messaging::receiveVoiceChatOnly(delivered);
            }, "voice-only receive pumps real sockets while normal actions are locked");
            require(delivered.sourceId == admitted.sourceId && delivered.sourceId != remote.sourceId &&
                delivered.binaryDataA == Stop, "ingress identity comes from the connection, never client metadata");
            require(messaging::receiveAction(delivered) && delivered.action == actions::Type::Tick,
                "voice-only receive preserves the blocked ordinary action");
            auto local = voice();
            local.sourceId = 1234;
            require(messaging::sendAction(local) && messaging::receiveAction(delivered) &&
                delivered.sourceId == 0, "local MESS actions always use host identity zero");
        }
        {
            MessagingSession session;
            Host host;
            require(messaging::sendAction(actions::Type::Tick, rules::BankPlayer,
                rules::BankPlayer), "seed client local startup queue");
            require(messaging::startNetwork(host.client()), "Messaging owns the real client");
            require(!messaging::serverMode() && !messaging::networkMode() &&
                messaging::currentQueueSize() == 0, "client startup clears local state and immediately relinquishes RULE");
            until([&] { return messaging::networkMode(); }, [&]
            {
                host.transport->pump();
                messaging::pumpNetwork();
            }, "Messaging client handshake deadline");
            (void)drain(*host.transport);
            require(!messaging::sendAction(actions::Type::RollDice, 0, rules::BankPlayer),
                "spectator client cannot submit gameplay commands through MESS");
            auto announced = notification();
            announced.numberD = 77;
            require(host.transport->send(announced), "host sends a voice notification");
            until([&] { return messaging::currentQueueSize() == 1; }, [&]
            {
                host.transport->pump();
                messaging::pumpNetwork();
            }, "notification arrives in client MESS");
            host.transport.reset();
            until([&] { return !messaging::networkMode(); }, [&] { messaging::pumpNetwork(); },
                "host loss deadline");
            actions::Message stale;
            require(!messaging::serverMode() && messaging::currentQueueSize() == 0 &&
                !messaging::receiveVoiceChatOnly(stale) && !messaging::sendAction(voice()),
                "host loss purges stale voice and client never becomes a local server");
        }
    }
}

int main()
{
    try
    {
        SocketRuntime sockets;
        const std::array tests{
            std::pair{"endpoint, admission, late join and disconnect", testEndpointAndAdmission},
            std::pair{"partial frames, incoming saturation and deferred STOP", testPartialFramesAndIncomingBound},
            std::pair{"invalid framing and spectator ingress rejection", testRejectedWireFrames},
            std::pair{"outgoing frame and byte bounds", testOutgoingBounds},
            std::pair{"large notification and Unicode round trip", testNotificationRoundTrip},
            std::pair{"late admission preserves full resync headroom", testAdmissionHeadroom},
            std::pair{"established voice continues during locked late admission", testEstablishedVoiceDuringPendingAdmission},
            std::pair{"real Messaging identity, animation gate and host loss", testMessagingIngressAndClientLoss}};
        for (const auto& [name, run] : tests)
        {
            run();
            std::cout << "[PASS] " << name << '\n';
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
