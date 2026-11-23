#pragma once
// Real LAN/Wi-Fi UDP transport for 3DS Local Wireless (nwm::UDS).
// Enables Wii U <-> Wii U (and compatible LAN UDS bridges) local multiplayer
// for games like New Super Mario Bros. 2 Co-op, Mario Kart 7 Local, etc.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
#include <thread>
#include <vector>

#ifdef __WIIU__
#include <arpa/inet.h>
#include <coreinit/dynload.h>
#include <coreinit/time.h>
#include <netinet/in.h>
#include <nsysnet/_socket.h>
#include <sys/socket.h>
#endif

#include "../kernel.h"
#include "../service.h"

namespace kernel::uds_lan {

constexpr u16 kUdsUdpPort = 28340;
constexpr u32 kUdsMaxNodes = 16;
constexpr u32 kAppDataMax = 0xC8;
constexpr u32 kMaxDataPayload = 0x5C6;

enum class NetworkStatus : u32 {
    NotConnected = 3,
    ConnectedAsHost = 6,
    Connecting = 7,
    ConnectedAsClient = 9,
    ConnectedAsSpectator = 10,
};

enum class DisconnectReason : u32 {
    None = 0,
    Connected = 1,
    NotConnected = 2,
    ConnectionLost = 4,
};

// Endian-safe helpers for guest little-endian and 3DS beacon big-endian fields.
inline u16 rle16(const u8 *p) { return (u16)p[0] | ((u16)p[1] << 8); }
inline u32 rle32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
inline u64 rle64(const u8 *p) { return (u64)rle32(p) | ((u64)rle32(p + 4) << 32); }

inline void wle16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
inline void wle32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
inline void wle64(u8 *p, u64 v) { wle32(p, (u32)v); wle32(p + 4, (u32)(v >> 32)); }

inline u16 rbe16(const u8 *p) { return ((u16)p[0] << 8) | (u16)p[1]; }
inline u32 rbe32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3]; }
inline u64 rbe64(const u8 *p) { return ((u64)rbe32(p) << 32) | (u64)rbe32(p + 4); }

inline void wbe16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
inline void wbe32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
inline void wbe64(u8 *p, u64 v) { wbe32(p, (u32)(v >> 32)); wbe32(p + 4, (u32)v); }

inline void sha1(const u8 *data, size_t len, u8 out[20]) {
    u32 h0 = 0x67452301u, h1 = 0xEFCDAB89u, h2 = 0x98BADCFEu, h3 = 0x10325476u, h4 = 0xC3D2E1F0u;
    std::vector<u8> msg(data, data + len);
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0);
    u64 bits = (u64)len * 8;
    for (int i = 7; i >= 0; i--) msg.push_back((u8)(bits >> (i * 8)));
    auto rotl = [](u32 x, int n) { return (x << n) | (x >> (32 - n)); };
    for (size_t off = 0; off < msg.size(); off += 64) {
        u32 w[80];
        for (int i = 0; i < 16; i++) w[i] = rbe32(&msg[off + i * 4]);
        for (int i = 16; i < 80; i++) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        u32 a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; i++) {
            u32 f, k;
            if (i < 20)      { f = (b & c) | ((~b) & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;            k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;            k = 0xCA62C1D6u; }
            u32 t = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = t;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    wbe32(out + 0, h0); wbe32(out + 4, h1); wbe32(out + 8, h2); wbe32(out + 12, h3); wbe32(out + 16, h4);
}

struct NodeInfo {
    u64 friend_code_seed = 0;
    std::array<u16, 10> username{};
    u16 network_node_id = 0;

    void clear() {
        friend_code_seed = 0;
        username.fill(0);
        network_node_id = 0;
    }
    // 40-byte 3DS IPC layout
    void serialize_le(u8 *dst) const {
        std::memset(dst, 0, 40);
        wle64(dst + 0x00, friend_code_seed);
        for (size_t i = 0; i < 10; i++) wle16(dst + 0x08 + i * 2, username[i]);
        wle16(dst + 0x20, network_node_id);
    }
    void deserialize_le(const u8 *src) {
        friend_code_seed = rle64(src + 0x00);
        for (size_t i = 0; i < 10; i++) username[i] = rle16(src + 0x08 + i * 2);
        network_node_id = rle16(src + 0x20);
    }
    void from_ipc(IPC &ipc, int start_word) {
        u8 buf[40];
        for (int i = 0; i < 10; i++) wle32(buf + i * 4, ipc[start_word + i]);
        deserialize_le(buf);
    }
    void to_ipc(IPC &ipc, int start_word) const {
        u8 buf[40];
        serialize_le(buf);
        for (int i = 0; i < 10; i++) ipc.set(start_word + i, rle32(buf + i * 4));
    }
};

struct NetworkInfo {
    std::array<u8, 0x108> raw{};

    void set_host_mac(const u8 mac[6]) { std::memcpy(raw.data(), mac, 6); }
    void get_host_mac(u8 mac[6]) const { std::memcpy(mac, raw.data(), 6); }
    u8 channel() const { return raw[6]; }
    void set_channel(u8 ch) { raw[6] = ch; }
    void set_oui_nintendo() { raw[0x0C] = 0x00; raw[0x0D] = 0x1F; raw[0x0E] = 0x32; raw[0x0F] = 21; }
    u32 wlan_comm_id() const { return rbe32(&raw[0x10]); }
    u8 id() const { return raw[0x14]; }
    u16 attributes() const { return rbe16(&raw[0x16]); }
    void set_attributes(u16 a) { wbe16(&raw[0x16], a); }
    u32 network_id() const { return rbe32(&raw[0x18]); }
    void set_network_id(u32 nid) { wbe32(&raw[0x18], nid); }
    u8 total_nodes() const { return raw[0x1C]; }
    void set_total_nodes(u8 n) { raw[0x1C] = n; }
    u8 max_nodes() const { return std::clamp<u8>(raw[0x1D], 1, (u8)kUdsMaxNodes); }
    u8 app_data_size() const { return std::min<u8>(raw[0x3F], (u8)kAppDataMax); }
    void set_app_data(const u8 *src, u8 sz) {
        u8 n = std::min<u8>(sz, (u8)kAppDataMax);
        raw[0x3F] = n;
        std::memset(&raw[0x40], 0, kAppDataMax);
        if (n && src) std::memcpy(&raw[0x40], src, n);
    }
};

inline std::vector<u8> build_beacon_frame(const NetworkInfo &net, const std::array<NodeInfo, kUdsMaxNodes> &nodes, u16 node_bitmask) {
    std::vector<u8> f;
    f.reserve(512);
    auto push8 = [&](u8 v) { f.push_back(v); };
    auto push_bytes = [&](const u8 *p, size_t n) { f.insert(f.end(), p, p + n); };

    // BeaconFrameHeader (12 bytes): timestamp (8), beacon_interval=100 (2), capabilities=0x0431 (2)
    u8 hdr[12]{};
    wle64(hdr + 0, 900000000ull);
    wle16(hdr + 8, 100);
    wle16(hdr + 10, 0x0431);
    push_bytes(hdr, 12);

    // SSID tag (10 bytes): tag 0, len 8, zeros
    push8(0); push8(8);
    for (int i = 0; i < 8; i++) push8(0);

    // Nintendo DummyTag (9 bytes)
    const u8 dummy_tag[9] = {0xDD, 0x07, 0x00, 0x1F, 0x32, 20, 0x0A, 0x00, 0x00};
    push_bytes(dummy_tag, 9);

    // NetworkInfoTag (54 + appdata_size bytes)
    u8 app_sz = net.app_data_size();
    push8(0xDD);
    push8((u8)(52 + app_sz));
    size_t ni_start = f.size();
    push_bytes(&net.raw[0x0C], 0x1F);  // OUI..reserved (31 bytes)
    for (int i = 0; i < 20; i++) push8(0);  // sha_hash placeholder
    push8(app_sz);
    if (app_sz) push_bytes(&net.raw[0x40], app_sz);
    u8 digest[20]{};
    sha1(&f[ni_start], f.size() - ni_start, digest);
    std::memcpy(&f[ni_start + 0x1F], digest, 20);

    // Payload for EncryptedData0 / EncryptedData1 (BeaconData 18 bytes + max_nodes * BeaconNodeInfo 30 bytes)
    u8 max_n = net.max_nodes();
    std::vector<u8> payload(18 + (size_t)max_n * 30, 0);
    wbe16(&payload[16], node_bitmask);
    for (u8 i = 0; i < max_n; i++) {
        u8 *bn = &payload[18 + (size_t)i * 30];
        wbe64(bn + 0, nodes[i].friend_code_seed);
        for (int j = 0; j < 10; j++) wbe16(bn + 8 + j * 2, nodes[i].username[j]);
        wbe16(bn + 28, nodes[i].network_node_id);
    }

    constexpr size_t kCut = 0xFA;
    size_t part0 = std::min(kCut, payload.size());
    push8(0xDD);
    push8((u8)(4 + part0));
    push8(0x00); push8(0x1F); push8(0x32); push8(24);  // EncryptedData0
    push_bytes(payload.data(), part0);

    if (payload.size() > kCut) {
        size_t part1 = payload.size() - kCut;
        push8(0xDD);
        push8((u8)(4 + part1));
        push8(0x00); push8(0x1F); push8(0x32); push8(25);  // EncryptedData1
        push_bytes(payload.data() + kCut, part1);
    }
    return f;
}

// Lightweight UDP socket wrapper for Wii U nsysnet.rpl
class UdpTransport {
public:
    bool init() {
        if (ready_) return true;
#ifdef __WIIU__
        if (tried_) return ready_;
        tried_ = true;
        OSDynLoad_Module m{};
        if (OSDynLoad_Acquire("nsysnet.rpl", &m) != OS_DYNLOAD_OK) return false;
        auto load = [&](const char *name, void **fn) -> bool {
            return OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, name, fn) == OS_DYNLOAD_OK && *fn;
        };
        if (!load("socket_lib_init", (void **)&p_init_) ||
            !load("socket", (void **)&p_socket_) ||
            !load("socketclose", (void **)&p_close_) ||
            !load("bind", (void **)&p_bind_) ||
            !load("setsockopt", (void **)&p_setsockopt_) ||
            !load("sendto", (void **)&p_sendto_) ||
            !load("recvfrom", (void **)&p_recvfrom_) ||
            !load("select", (void **)&p_select_)) {
            return false;
        }
        p_init_();

        // Query Wii U local IP & subnet if nn_ac is available
        OSDynLoad_Module acm{};
        if (OSDynLoad_Acquire("nn_ac.rpl", &acm) == OS_DYNLOAD_OK) {
            int (*ac_init)() = nullptr;
            int (*ac_ip)(u32 *) = nullptr;
            int (*ac_mask)(u32 *) = nullptr;
            if (OSDynLoad_FindExport(acm, OS_DYNLOAD_EXPORT_FUNC, "ACInitialize", (void **)&ac_init) == OS_DYNLOAD_OK && ac_init)
                ac_init();
            if (OSDynLoad_FindExport(acm, OS_DYNLOAD_EXPORT_FUNC, "ACGetAssignedAddress", (void **)&ac_ip) == OS_DYNLOAD_OK && ac_ip)
                ac_ip(&local_ip_be_);
            if (OSDynLoad_FindExport(acm, OS_DYNLOAD_EXPORT_FUNC, "ACGetAssignedSubnet", (void **)&ac_mask) == OS_DYNLOAD_OK && ac_mask)
                ac_mask(&local_mask_be_);
        }

        fd_ = p_socket_(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd_ < 0) return false;

        int one = 1;
        p_setsockopt_(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        p_setsockopt_(fd_, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
        p_setsockopt_(fd_, SOL_SOCKET, SO_NBIO, &one, sizeof(one));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(kUdsUdpPort);
        if (p_bind_(fd_, (const sockaddr *)&addr, sizeof(addr)) < 0) {
            p_close_(fd_);
            fd_ = -1;
            return false;
        }

        // Stable virtual MAC per console
        u32 seed = local_ip_be_ ? local_ip_be_ : (u32)OSGetTick();
        local_mac_ = {0x02, 0x3D, 0x11, (u8)(seed >> 16), (u8)(seed >> 8), (u8)seed};
        ready_ = true;
        return true;
#else
        return false;
#endif
    }

    void close() {
#ifdef __WIIU__
        if (fd_ >= 0 && p_close_) {
            p_close_(fd_);
            fd_ = -1;
        }
#endif
        ready_ = false;
        tried_ = false;
    }

    bool ready() const { return ready_; }
    const std::array<u8, 6> &local_mac() const { return local_mac_; }
    u32 local_ip_be() const { return local_ip_be_; }

    void send_unicast(u32 ip_be, u16 port_be, const u8 *data, size_t len) {
#ifdef __WIIU__
        if (!ready_ || fd_ < 0 || !ip_be) return;
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_addr.s_addr = ip_be;
        dst.sin_port = port_be ? port_be : htons(kUdsUdpPort);
        p_sendto_(fd_, data, (int)len, 0, (const sockaddr *)&dst, sizeof(dst));
#else
        (void)ip_be; (void)port_be; (void)data; (void)len;
#endif
    }

    void send_broadcast(const u8 *data, size_t len) {
#ifdef __WIIU__
        if (!ready_ || fd_ < 0) return;
        send_unicast(0xFFFFFFFFu, htons(kUdsUdpPort), data, len);
        if (local_ip_be_ && local_mask_be_) {
            u32 sub_bcast = local_ip_be_ | ~local_mask_be_;
            if (sub_bcast != 0xFFFFFFFFu) send_unicast(sub_bcast, htons(kUdsUdpPort), data, len);
        }
#else
        (void)data; (void)len;
#endif
    }

    int recv(u8 *buf, size_t cap, u32 &src_ip_be, u16 &src_port_be) {
#ifdef __WIIU__
        if (!ready_ || fd_ < 0 || fd_ >= NSYSNET_FD_SETSIZE) return -1;
        nsysnet_fd_set rfds{};
        NSYSNET_FD_ZERO(&rfds);
        NSYSNET_FD_SET(fd_, &rfds);
        nsysnet_timeval tv{0, 0};
        if (p_select_(fd_ + 1, &rfds, nullptr, nullptr, &tv) <= 0) return -1;
        sockaddr_in src{};
        int slen = sizeof(src);
        int n = p_recvfrom_(fd_, buf, (int)cap, MSG_DONTWAIT, (sockaddr *)&src, &slen);
        if (n > 0) {
            src_ip_be = src.sin_addr.s_addr;
            src_port_be = src.sin_port;
        }
        return n;
#else
        (void)buf; (void)cap; (void)src_ip_be; (void)src_port_be;
        return -1;
#endif
    }

private:
    bool tried_ = false;
    bool ready_ = false;
    int fd_ = -1;
    u32 local_ip_be_ = 0;
    u32 local_mask_be_ = 0;
    std::array<u8, 6> local_mac_{0x02, 0x3D, 0x11, 0x00, 0x00, 0x01};
#ifdef __WIIU__
    void (*p_init_)() = nullptr;
    int (*p_socket_)(int, int, int) = nullptr;
    int (*p_close_)(int) = nullptr;
    int (*p_bind_)(int, const struct sockaddr *, int) = nullptr;
    int (*p_setsockopt_)(int, int, int, const void *, int) = nullptr;
    int (*p_sendto_)(int, const void *, int, int, const struct sockaddr *, int) = nullptr;
    int (*p_recvfrom_)(int, void *, int, int, struct sockaddr *, int *) = nullptr;
    int (*p_select_)(int, nsysnet_fd_set *, nsysnet_fd_set *, nsysnet_fd_set *, nsysnet_timeval *) = nullptr;
#endif
};

}  // namespace kernel::uds_lan
