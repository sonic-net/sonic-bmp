#include "parseBMP.h"
#include "BMPReader.h"
#include "bgp_common.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {

using Bytes = std::vector<u_char>;

void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Function>
void rejects(Function function) {
    try {
        function();
    } catch (const char *) {
        return;
    }
    throw std::runtime_error("Malformed message was accepted");
}

void append(Bytes &destination, const Bytes &source) {
    destination.insert(destination.end(), source.begin(), source.end());
}

Bytes tlv(uint16_t type, const Bytes &value) {
    check(value.size() <= 65535, "Test TLV is too large");
    const uint16_t length = static_cast<uint16_t>(value.size());
    Bytes result = {static_cast<u_char>(type >> 8), static_cast<u_char>(type),
                    static_cast<u_char>(length >> 8), static_cast<u_char>(length)};
    append(result, value);
    return result;
}

Bytes message(u_char type, const Bytes &payload) {
    const uint32_t length = static_cast<uint32_t>(payload.size() + 6);
    Bytes result = {3, static_cast<u_char>(length >> 24),
                    static_cast<u_char>(length >> 16), static_cast<u_char>(length >> 8),
                    static_cast<u_char>(length), type};
    append(result, payload);
    return result;
}

class SocketPair {
public:
    int reader;
    int writer;
    SocketPair() {
        int sockets[2];
        check(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0, "socketpair failed");
        reader = sockets[0];
        writer = sockets[1];
        timeval timeout = {2, 0};
        check(setsockopt(reader, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0,
              "Socket timeout setup failed");
    }
    ~SocketPair() {
        if (reader >= 0) close(reader);
        close(writer);
    }
    void send(const Bytes &bytes) {
        size_t written = 0;
        while (written < bytes.size()) {
            const ssize_t count = ::send(writer, bytes.data() + written,
                                         bytes.size() - written, MSG_NOSIGNAL);
            if (count < 0 && errno == EINTR)
                continue;
            check(count > 0, "Socket write failed");
            written += static_cast<size_t>(count);
        }
    }
};

struct Fixture {
    Logger logger;
    MsgBusInterface::obj_bgp_peer peer;
    parseBMP parser;
    MsgBusInterface::obj_router router;

    Fixture() : logger("/dev/null", "/dev/null"), peer{}, parser(&logger, &peer), router{} {
        std::strcpy(router.initiate_data, "old-value");
        std::strcpy(reinterpret_cast<char *>(router.name), "old-value");
        std::memcpy(router.descr, "old-value", sizeof("old-value"));
        std::strcpy(router.term_data, "old-value");
        std::memcpy(peer.table_name, "old-value", sizeof("old-value"));
    }
    void parse(u_char type, const Bytes &payload) {
        if (type == parseBMP::TYPE_PEER_UP) {
            Bytes copy = payload;
            parser.parsePeerUpInfo(copy.data(), static_cast<int>(copy.size()));
        } else {
            SocketPair sockets;
            sockets.send(message(type, payload));
            shutdown(sockets.writer, SHUT_WR);
            check(parser.handleMessage(sockets.reader) == type, "Wrong message type");
            if (type == parseBMP::TYPE_INIT_MSG)
                parser.handleInitMsg(sockets.reader, router);
            else
                parser.handleTermMsg(sockets.reader, router);
        }
    }
};

struct StringField {
    const char *name;
    u_char message_type;
    uint16_t tlv_type;
    size_t capacity;
    std::function<const char *(const Fixture &)> value;
};

const std::vector<StringField> fields = {
    {"init-free", 4, 0, 4096, [](const Fixture &f) { return f.router.initiate_data; }},
    {"init-name", 4, 2, 255, [](const Fixture &f) {
        return reinterpret_cast<const char *>(f.router.name); }},
    {"init-descr", 4, 1, 255, [](const Fixture &f) {
        return reinterpret_cast<const char *>(f.router.descr); }},
    {"term-free", 5, 0, 4096, [](const Fixture &f) { return f.router.term_data; }},
    {"peer-vrf", 3, 3, 255, [](const Fixture &f) {
        return reinterpret_cast<const char *>(f.peer.table_name); }}
};

class RecordingBus : public MsgBusInterface {
public:
    int init = 0, up = 0, remote_term = 0, local_error = 0, raw = 0;
    obj_router router{};
    obj_bgp_peer peer{};
    void update_Collector(obj_collector &, collector_action_code) override {}
    void update_Router(obj_router &value, router_action_code code) override {
        if (code == ROUTER_ACTION_INIT) { ++init; router = value; }
        if (code == ROUTER_ACTION_TERM) {
            if (value.term_reason_code == parseBMP::TERM_REASON_OPENBMP_CONN_ERR)
                ++local_error;
            else
                ++remote_term;
        }
    }
    void update_Peer(obj_bgp_peer &value, obj_peer_up_event *, obj_peer_down_event *,
                     peer_action_code code) override {
        if (code == PEER_ACTION_UP) { ++up; peer = value; }
    }
    void update_baseAttribute(obj_bgp_peer &, obj_path_attr &, base_attr_action_code) override {}
    void update_unicastPrefix(obj_bgp_peer &, std::vector<obj_rib> &, obj_path_attr *,
                              unicast_prefix_action_code) override {}
    void update_L3Vpn(obj_bgp_peer &, std::vector<obj_vpn> &, obj_path_attr *,
                      vpn_action_code) override {}
    void update_eVPN(obj_bgp_peer &, std::vector<obj_evpn> &, obj_path_attr *,
                     vpn_action_code) override {}
    void add_StatReport(obj_bgp_peer &, obj_stats_report &) override {}
    void update_LsNode(obj_bgp_peer &, obj_path_attr &, std::list<obj_ls_node> &,
                       ls_action_code) override {}
    void update_LsLink(obj_bgp_peer &, obj_path_attr &, std::list<obj_ls_link> &,
                       ls_action_code) override {}
    void update_LsPrefix(obj_bgp_peer &, obj_path_attr &, std::list<obj_ls_prefix> &,
                         ls_action_code) override {}
    void send_bmp_raw(u_char *, obj_bgp_peer &, u_char *, size_t) override { ++raw; }
};

Bytes peerUpPayload(const Bytes &information) {
    Bytes payload(BMP_PEER_HDR_LEN + BMP_PEER_UP_HDR_LEN, 0);
    payload[1] = 0x80; // IPv4 peer.
    Bytes open(16, 0xff);
    append(open, {0, 29, 1, 4, 0xfd, 0xe8, 0, 90, 192, 0, 2, 1, 0});
    append(payload, open);
    append(payload, open);
    append(payload, information);
    return payload;
}

struct ReaderFixture {
    Logger logger;
    Config config;
    BMPReader reader;
    RecordingBus bus;
    SocketPair sockets;
    BMPListener::ClientInfo client{};
    ReaderFixture() : logger("/dev/null", "/dev/null"), config(), reader(&logger, &config) {
        config.pat_enabled = false;
        client.c_sock = sockets.reader;
        std::strcpy(client.c_ip, "192.0.2.1");
    }
    ~ReaderFixture() {
        // BMPReader closes the descriptor on termination/error.
        if (client.c_sock == 0 || bus.remote_term)
            sockets.reader = -1;
    }
};

struct Case {
    std::string name;
    bool positive;
    std::function<void()> function;
};

}

int main(int argc, char **argv) {
    std::vector<Case> cases;
    auto add = [&](const std::string &name, bool positive, std::function<void()> function) {
        cases.push_back({name, positive, function});
    };
    for (const auto &field : fields) {
        for (const size_t length : {size_t(0), size_t(1), field.capacity - 1,
                                   field.capacity, field.capacity + 1, field.capacity + 2,
                                   size_t(8192), size_t(65531)}) {
            add(std::string(field.name) + "-length-" + std::to_string(length),
                length < field.capacity && (length > 0 || field.message_type == 4), [=] {
                Fixture f;
                if (length > 0) {
                    std::memset(f.router.initiate_data, 0, sizeof(f.router.initiate_data));
                    std::memset(f.router.name, 0, sizeof(f.router.name));
                    std::memset(f.router.descr, 0, sizeof(f.router.descr));
                    std::memset(f.router.term_data, 0, sizeof(f.router.term_data));
                    std::memset(f.peer.table_name, 0, sizeof(f.peer.table_name));
                }
                f.parse(field.message_type, tlv(field.tlv_type, Bytes(length, 'x')));
                const std::string expected = length == 0 ? "old-value" :
                    std::string(std::min(length, field.capacity - 1), 'x');
                check(std::string(field.value(f)) == expected, "String bounds/termination mismatch");
                check(f.router.term_reason_code == 0, "Copy corrupted adjacent metadata");
            });
        }
        add(std::string(field.name) + "-duplicate-shorter", false, [=] {
            Fixture f;
            Bytes payload = tlv(field.tlv_type, {'a', 'b', 'c', 'd'});
            append(payload, tlv(field.tlv_type, {'x'}));
            append(payload, tlv(field.tlv_type, {}));
            f.parse(field.message_type, payload);
            check(std::string(field.value(f)) == "x", "Duplicate value retained stale tail");
        });
        add(std::string(field.name) + "-embedded-nul", true, [=] {
            Fixture f;
            f.parse(field.message_type, tlv(field.tlv_type, {'a', 0, 'b'}));
            check(std::string(field.value(f)) == "a", "Embedded-NUL string semantics changed");
        });
        add(std::string(field.name) + "-long-unknown-before-known", false, [=] {
            Fixture f;
            Bytes payload = tlv(100, Bytes(8192, 0));
            append(payload, tlv(field.tlv_type, {'o', 'k'}));
            f.parse(field.message_type, payload);
            check(std::string(field.value(f)) == "ok", "TLV cursor advanced by clipped length");
        });
        add(std::string(field.name) + "-long-known-before-known", false, [=] {
            Fixture f;
            Bytes payload = tlv(field.tlv_type, Bytes(8192, 'x'));
            append(payload, tlv(field.tlv_type, {'o', 'k'}));
            f.parse(field.message_type, payload);
            check(std::string(field.value(f)) == "ok", "Long string desynchronized later TLVs");
        });
        for (int tail = 1; tail <= 3; ++tail) {
            add(std::string(field.name) + "-truncated-header-" + std::to_string(tail), false, [=] {
                Fixture f;
                const auto before_router = f.router;
                const auto before_peer = f.peer;
                Bytes payload = tlv(field.tlv_type, {'x'});
                append(payload, Bytes(tail, 0));
                rejects([&] { f.parse(field.message_type, payload); });
                check(std::memcmp(&f.router, &before_router, sizeof(f.router)) == 0,
                      "Malformed message partially changed router metadata");
                check(std::memcmp(&f.peer, &before_peer, sizeof(f.peer)) == 0,
                      "Malformed message partially changed peer metadata");
            });
        }
        add(std::string(field.name) + "-truncated-value", false, [=] {
            Fixture f;
            const auto before_router = f.router;
            const auto before_peer = f.peer;
            Bytes payload = tlv(field.tlv_type, {'o', 'k'});
            Bytes truncated = tlv(field.tlv_type, {'x'});
            truncated[3] = 2;
            append(payload, truncated);
            rejects([&] { f.parse(field.message_type, payload); });
            check(std::memcmp(&f.router, &before_router, sizeof(f.router)) == 0,
                  "Truncated value partially changed router metadata");
            check(std::memcmp(&f.peer, &before_peer, sizeof(f.peer)) == 0,
                  "Truncated value partially changed peer metadata");
        });
    }
    // Leave room for a valid BGP ID and sysName around the ignored TLV.
    const int max_bgp_id_length = BGP_MAX_MSG_SIZE - 3 * BMP_INFO_TLV_HDR_LEN - 4 - 1;
    for (int length : {0, 1, 2, 3, 4, 5, 255, 256, 8192, max_bgp_id_length}) {
        add("bgp-id-width-" + std::to_string(length), length <= 5, [=] {
            Fixture f;
            Bytes value(length, 1);
            if (length != 4) {
                Bytes payload = tlv(2, {'n'});
                append(payload, tlv(65531, value));
                append(payload, tlv(1, {'d'}));
                f.parse(4, payload);
                check(std::string(reinterpret_cast<const char *>(f.router.name)) == "n" &&
                      std::string(reinterpret_cast<const char *>(f.router.descr)) == "d",
                      "Ignored BGP ID lost surrounding metadata");
                check(f.router.hash_type == 1 && f.router.bgp_id[0] == '\0',
                      "Ignored BGP ID changed router identity");

                payload = tlv(65531, {192, 0, 2, 9});
                append(payload, tlv(65531, value));
                append(payload, tlv(2, {'n'}));
                f.parse(4, payload);
                check(std::string(f.router.bgp_id) == "192.0.2.9" && f.router.hash_type == 2,
                      "Ignored BGP ID overrode a valid BGP ID");
            } else {
                f.parse(4, tlv(65531, {192, 0, 2, 9}));
                check(std::string(f.router.bgp_id) == "192.0.2.9", "BGP ID not decoded");
                check(f.router.hash_type == 2, "BGP ID hash precedence changed");
            }
        });
        if (length != 4) {
            add("reader-ignored-bgp-id-width-" + std::to_string(length), length <= 5, [=] {
                ReaderFixture f;
                Bytes payload = tlv(2, {'n'});
                append(payload, tlv(65531, Bytes(length, 1)));
                append(payload, tlv(1, {'d'}));
                Bytes packet = message(4, payload);
                append(packet, message(4, tlv(2, {'m'})));
                f.sockets.send(packet);
                check(f.reader.ReadIncomingMsg(&f.client, &f.bus),
                      "Unsupported BGP ID closed the session");
                check(f.client.initRec && f.client.c_sock == f.sockets.reader &&
                      f.bus.init == 1 && f.bus.local_error == 0 && f.bus.raw == 1,
                      "Unsupported BGP ID did not publish a successful Initiation");
                check(std::string(reinterpret_cast<const char *>(f.bus.router.name)) == "n" &&
                      std::string(reinterpret_cast<const char *>(f.bus.router.descr)) == "d" &&
                      f.bus.router.bgp_id[0] == '\0' && f.bus.router.hash_type == 1,
                      "Unsupported BGP ID corrupted published metadata");
                check(f.reader.ReadIncomingMsg(&f.client, &f.bus),
                      "Session could not process the next message");
                check(f.bus.init == 2 && f.bus.raw == 2 && f.bus.local_error == 0 &&
                      std::string(reinterpret_cast<const char *>(f.bus.router.name)) == "m",
                      "Message after unsupported BGP ID was not published");
            });
        }
    }
    add("init-bgp-id-truncated-value", false, [] {
        Fixture f;
        const auto before = f.router;
        Bytes payload = tlv(2, {'n'});
        Bytes truncated = tlv(65531, {192, 0, 2});
        truncated[3] = 4;
        append(payload, truncated);
        rejects([&] { f.parse(4, payload); });
        check(std::memcmp(&f.router, &before, sizeof(f.router)) == 0,
              "Truncated BGP ID partially changed router metadata");
    });
    add("reader-truncated-bgp-id-value", false, [] {
        ReaderFixture f;
        Bytes payload = tlv(2, {'n'});
        Bytes truncated = tlv(65531, {192, 0, 2});
        truncated[3] = 4;
        append(payload, truncated);
        f.sockets.send(message(4, payload));
        shutdown(f.sockets.writer, SHUT_WR);
        rejects([&] { f.reader.ReadIncomingMsg(&f.client, &f.bus); });
        check(f.bus.init == 0 && f.bus.local_error == 1 && f.bus.raw == 0 &&
              f.client.c_sock == 0 && !f.client.initRec,
              "Truncated BGP ID did not follow the disconnect/error path");
    });
    for (int length = 0; length <= 3; ++length) {
        add("reason-width-" + std::to_string(length), length == 2, [=] {
            Fixture f;
            if (length != 2) {
                const auto before = f.router;
                Bytes payload = tlv(0, {'n'});
                append(payload, tlv(1, Bytes(length, 0)));
                rejects([&] { f.parse(5, payload); });
                check(std::memcmp(&f.router, &before, sizeof(f.router)) == 0,
                      "Invalid reason width partially changed router metadata");
            } else {
                f.parse(5, tlv(1, {0, 2}));
                check(f.router.term_reason_code == 2, "Termination reason not decoded");
            }
        });
    }
    add("peer-negative-length", false, [] {
        Fixture f;
        rejects([&] { f.parser.parsePeerUpInfo(nullptr, -1); });
    });
    add("peer-null-positive-length", false, [] {
        Fixture f;
        rejects([&] { f.parser.parsePeerUpInfo(nullptr, 4); });
    });
    add("peer-empty-information", true, [] {
        Fixture f;
        f.parser.parsePeerUpInfo(nullptr, 0);
        check(std::string(reinterpret_cast<const char *>(f.peer.table_name)) == "old-value",
              "Empty peer data changed metadata");
    });
    add("init-bgp-id-precedence", true, [] {
        Fixture f;
        Bytes payload = tlv(65531, {192, 0, 2, 9});
        append(payload, tlv(2, {'n'}));
        f.parse(4, payload);
        check(f.router.hash_type == 2, "Name overrode BGP ID hashing");
    });
    for (const u_char type : {u_char(4), u_char(5), u_char(3)}) {
        add("reader-malformed-type-" + std::to_string(type), false, [=] {
            ReaderFixture f;
            Bytes payload = {0, 0, 0};
            if (type == 3) payload = peerUpPayload(payload);
            f.sockets.send(message(type, payload));
            shutdown(f.sockets.writer, SHUT_WR);
            rejects([&] { f.reader.ReadIncomingMsg(&f.client, &f.bus); });
            check(f.bus.init == 0 && f.bus.up == 0 && f.bus.remote_term == 0,
                  "Malformed message published a successful event");
            check(f.bus.local_error == 1 && f.bus.raw == 0 && f.client.c_sock == 0,
                  "Malformed session did not follow disconnect/error path");
            check(!f.client.initRec, "Malformed Init marked as received");
        });
    }
    add("reader-valid-peer-up", true, [] {
        ReaderFixture f;
        f.sockets.send(message(3, peerUpPayload(tlv(3, {'v', 'r', 'f'}))));
        check(f.reader.ReadIncomingMsg(&f.client, &f.bus), "Valid peer session closed");
        check(f.bus.up == 1 &&
              std::string(reinterpret_cast<const char *>(f.bus.peer.table_name)) == "vrf",
              "Valid peer metadata not published");
    });
    add("reader-short-peer-header", false, [] {
        ReaderFixture f;
        f.sockets.send(message(3, Bytes(BMP_PEER_HDR_LEN + BMP_PEER_UP_HDR_LEN - 1, 0)));
        shutdown(f.sockets.writer, SHUT_WR);
        rejects([&] { f.reader.ReadIncomingMsg(&f.client, &f.bus); });
        check(f.bus.up == 0 && f.bus.local_error == 1, "Short peer header was accepted");
    });
    add("reader-multiple-messages", true, [] {
        ReaderFixture f;
        Bytes packet = message(4, tlv(2, {'n'}));
        append(packet, message(5, tlv(1, {0, 0})));
        f.sockets.send(packet);
        check(f.reader.ReadIncomingMsg(&f.client, &f.bus), "Valid Init closed session");
        check(f.client.initRec && f.bus.init == 1, "Init metadata was not committed");
        check(!f.reader.ReadIncomingMsg(&f.client, &f.bus), "Termination did not close session");
        check(f.bus.remote_term == 1 && f.bus.raw == 2, "Valid events not published");
    });
    add("reader-split-message", true, [] {
        ReaderFixture f;
        const Bytes packet = message(4, tlv(2, {'n'}));
        f.sockets.send(Bytes(packet.begin(), packet.begin() + 3));
        auto writer = std::async(std::launch::async, [&] {
            f.sockets.send(Bytes(packet.begin() + 3, packet.end()));
        });
        check(f.reader.ReadIncomingMsg(&f.client, &f.bus), "Split Init was rejected");
        writer.get();
        check(f.bus.init == 1 && f.bus.raw == 1, "Split Init did not publish exactly once");
    });
    const bool positive_only = argc == 2 && std::string(argv[1]) == "--positive";
    const std::string selected = argc == 3 && std::string(argv[1]) == "--case" ? argv[2] : "";
    int passed = 0, failed = 0;
    for (const auto &test : cases) {
        if ((positive_only && !test.positive) || (!selected.empty() && test.name != selected))
            continue;
        try {
            test.function();
            ++passed;
        } catch (const std::exception &error) {
            std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
            ++failed;
        } catch (const char *error) {
            std::cerr << "FAIL " << test.name << ": " << error << '\n';
            ++failed;
        }
    }
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed || passed == 0 ? 1 : 0;
}
