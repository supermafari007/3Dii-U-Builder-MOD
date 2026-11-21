// Small services: ptm, ndm, ac, frd, boss, cecd, news, ir, mic, cam, y2r, ldr:ro, soc, ssl, http, ...
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <deque>
#include <thread>
#include <vector>

#include "../loader.h"
#include "../service.h"
#include "services.h"
#include "uds_lan.hpp"

// The host's microphone: silence, unless a port provides one
__attribute__((weak)) void mic_host_start() {}
__attribute__((weak)) void mic_host_stop() {}
__attribute__((weak)) void mic_host_read(s16 *out, u32 n, u32 rate) { memset(out, 0, n * 2); }

namespace kernel {

std::vector<std::array<u8, 0x60>> g_host_miis;
int g_host_my_mii = -1;

u16 mii_crc16(const u8 *p, size_t n) {
    u16 c = 0;
    for (size_t i = 0; i < n; i++) {
        c ^= (u16)(p[i] << 8);
        for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (u16)((c << 1) ^ 0x1021) : (u16)(c << 1);
    }
    return c;
}

// A host Mii as a 3DS game reads it. Two layouts are taken, told apart by where the version (3) stands. The 3DS's own, which a Mii also
// has wherever the two consoles exchange one (a QR code, an account's Mii on its server): the version first, the ids as they read, every
// other field little-endian, UTF-16LE names, a CRC-16 over 0x00-0x5D at 0x5E. Its bytes go across. And the one the Wii U's own processor
// reads (wut's nn/ffl/miidata.h), which is what its software keeps in memory: the version last of the first four bytes, and every 16-bit
// field from 0x18 on the other way round, but for height and build, a byte each. That one is turned back first. Copying is allowed:
// Tomodachi Life's look-alikes need it.
bool mii_3ds(const std::array<u8, 0x60> &h, std::array<u8, 0x60> &m) {
    if (mii_crc16(h.data(), 0x5E) != (u16)(h[0x5E] << 8 | h[0x5F])) return false;  // the host's own copy must check out
    m = h;
    if (m[0] != 3 && m[3] == 3) {
        std::reverse(m.begin(), m.begin() + 4);
        for (size_t at = 0x18; at < 0x5C; at += 2)
            if (at != 0x2E) std::swap(m[at], m[at + 1]);  // height and build are a byte each
    }
    if (m[0] != 3 || !(m[0x0C] | m[0x0D] | m[0x0E] | m[0x0F])) return false;  // not version 3, or no Mii id (an empty slot)
    m[0x01] |= 1;
    // Born on a console that came after the 3DS (4: a Wii U, as its accounts' Miis are), it is said to be a 3DS's: Super Mario 3D Land's Mii
    // library, of 2011, counts no such Mii in Mii Maker ("You don't have any Mii characters") and drops its face from a save file at the
    // next start.
    if ((m[0x03] >> 4 & 7) > 3) m[0x03] = (u8)((m[0x03] & 0x8F) | 0x30);
    m[0x5C] = m[0x5D] = 0;
    const u16 crc = mii_crc16(m.data(), 0x5E);
    m[0x5E] = (u8)(crc >> 8);
    m[0x5F] = (u8)crc;
    return true;
}

// The console's Miis as 3DS games read them, the accounts' first and each one once (an account's Mii is in the console's Mii Maker too).
// Made once, at the first call: the frontend has read the console's Miis before the services start.
namespace {
struct ConsoleMiis {
    std::vector<std::array<u8, 0x60>> list;
    int mine = -1;  // the current account's
    ConsoleMiis() {
        for (size_t i = 0; i < g_host_miis.size(); i++) {
            std::array<u8, 0x60> m;
            if (!mii_3ds(g_host_miis[i], m)) continue;
            size_t at = 0;
            while (at < list.size() && memcmp(&list[at][0x0C], &m[0x0C], 10)) at++;  // the same Mii: its id and the console that made it
            if ((int)i == g_host_my_mii) mine = (int)at;
            if (at == list.size()) list.push_back(m);
        }
    }
};
}  // namespace

const std::vector<std::array<u8, 0x60>> &console_miis(int *mine) {
    static const ConsoleMiis miis;
    if (mine) *mine = miis.mine;
    return miis.list;
}

// The player's Mii: the current account's (the console's first, if that account has none a game can take), naming the place it has in
// Mii Maker's database (fs.cpp). With no Mii on the console at all, a plain male Mii named "Mario" (made-up console and Mii ids and a
// placeholder MAC; games mostly check the flags and the checksum).
std::array<u8, 0x60> player_mii() {
    int mine;
    const auto &miis = console_miis(&mine);
    if (!miis.empty()) {
        const u32 n = mine < 0 ? 0 : (u32)mine;
        std::array<u8, 0x60> mii = miis[n];
        if (const int place = mii_maker_place(n); place >= 0) mii[0x02] = (u8)(place / 10 | place % 10 << 4);
        const u16 crc = mii_crc16(mii.data(), 0x5E);
        mii[0x5E] = (u8)(crc >> 8);
        mii[0x5F] = (u8)crc;
        return mii;
    }
    std::array<u8, 0x60> m{};
    auto be32 = [&](size_t o, u32 v) { m[o] = (u8)(v >> 24); m[o + 1] = (u8)(v >> 16); m[o + 2] = (u8)(v >> 8); m[o + 3] = (u8)v; };
    auto be16 = [&](size_t o, u16 v) { m[o] = (u8)(v >> 8); m[o + 1] = (u8)v; };
    m[0] = 0x03;
    m[1] = 0x01;  // copying allowed (games that make look-alikes, e.g. Tomodachi Life, refuse it otherwise)
    m[2] = 0x10;
    m[3] = 0x30;
    const u64 sys = 0x3D5000000000C0DEull;  // console id of the "author"
    for (int i = 0; i < 8; i++) m[4 + i] = (u8)(sys >> (56 - 8 * i));
    be32(0x0C, 0x98000001);  // Mii id: normal Mii flags, creation time 1
    const u8 mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};  // locally administered placeholder
    memcpy(&m[0x10], mac, 6);
    be16(0x18, 0xA600);
    const char *name = "Mario";
    for (int i = 0; name[i]; i++) m[0x1A + 2 * i] = (u8)name[i];
    m[0x2E] = 0x40;
    m[0x2F] = 0x40;
    m[0x32] = 0x21;
    m[0x33] = 0x01;
    be32(0x34, 0x02684418);
    be32(0x38, 0x26344614);
    be16(0x3C, 0x8112);
    be16(0x3E, 0x1768);
    be16(0x40, 0x0D00);
    be16(0x42, 0x0029);
    be16(0x44, 0x0052);
    be16(0x46, 0x4850);
    const char *author = "Player";
    for (int i = 0; author[i]; i++) m[0x48 + 2 * i] = (u8)author[i];
    be16(0x5E, mii_crc16(m.data(), 0x5E));
    return m;
}

struct Ptm : Service {
    using Service::Service;
    void handle(IPC &ipc) override {
        switch (ipc.cmd()) {
        case 0x0005:  // GetAdapterState
        case 0x0006:  // GetShellState (open)
        case 0x0008:  // GetBatteryChargeState
            ipc.ok(2);
            ipc.set(2, 1);
            break;
        case 0x0007:  // GetBatteryLevel
            ipc.ok(2);
            ipc.set(2, 5);
            break;
        case 0x0009:  // GetPedometerState
            ipc.ok(2);
            ipc.set(2, 0);
            break;
        case 0x000B: {  // GetStepHistory(hours, start time u64, mapped buffer)
            u32 n = ipc[1];
            memset(gptr(ipc[5]), 0, n * 2);
            ipc.ok(1, 2);
            ipc.set(2, ((n * 2) << 4) | 0xC);
            ipc.set(3, ipc[5]);
            break;
        }
        case 0x000C:  // GetTotalStepCount
            ipc.ok(2);
            ipc.set(2, 0);
            break;
        case 0x0818:  // ptm:sysm ConfigureNew3DSCPU (libctru osSetSpeedupEnable): an old 3DS ignores it
            ipc.ok();
            break;
        case 0x0001:
        case 0x0002:
        case 0x0003:
        case 0x0004:
        default:
            unimplemented(ipc);
        }
    }
};

#ifndef MK7_PRETENDO_ONLINE
struct Ac : Service {
    using Service::Service;
    void handle(IPC &ipc) override {
        switch (ipc.cmd()) {
        case 0x0001: {  // CreateDefaultConfig -> static buffer 0x200
            u32 dst = ipc.static_buffer(0);
            memset(gptr(dst), 0, 0x200);
            ipc.ok(1, 2);
            ipc.static_desc(2, 0, 0x200);
            break;
        }
        case 0x0004:  // ConnectAsync
            ipc.ok();
            if (auto ev = get<Event>(ipc[5])) ev->signal();
            break;
        case 0x0005:  // GetConnectResult
            ipc.result(0xE0A09D2E);  // not connected
            break;
        case 0x000D:  // GetStatus
        case 0x000F:  // GetWifiStatus
        case 0x003E:  // IsConnected
            ipc.ok(2);
            ipc.set(2, 0);
            break;
        case 0x000E:  // GetLastErrorCode
            ipc.ok(2);
            ipc.set(2, 0);
            break;
        default:
            unimplemented(ipc);
        }
    }
};

struct Frd : Service {
    using Service::Service;
    void handle(IPC &ipc) override {
        switch (ipc.cmd()) {
        case 0x0001:  // HasLoggedIn
        case 0x0002:  // IsOnline
            ipc.ok(2);
            ipc.set(2, 0);
            break;
        // numbered as Azahar's frd_u.cpp has them (these were one or two off before, and GetMyMii was missing)
        case 0x0005:  // GetMyFriendKey
            ipc.ok(5);
            ipc.set(2, 0);
            ipc.set(3, 0);
            ipc.set64(4, 0);
            break;
        case 0x0006:  // GetMyPreference
            ipc.ok(4);
            ipc.set(2, 0);
            ipc.set(3, 0);
            ipc.set(4, 0);
            break;
        case 0x0007:  // GetMyProfile
            ipc.ok(3);
            ipc.set(2, 0);
            ipc.set(3, 0);
            break;
        case 0x0008: {  // GetMyPresence -> static buffer
            u32 dst = ipc.static_buffer(0);
            memset(gptr(dst), 0, 0x12C);
            ipc.ok(1, 2);
            ipc.static_desc(2, 0, 0x12C);
            break;
        }
        case 0x0009: {  // GetMyScreenName: the player's Mii's name
            const auto mii = player_mii();
            ipc.ok(7);
            memset(gptr(ipc.buf + 8), 0, 24);
            for (u32 i = 0; i < 10; i++) w16(ipc.buf + 8 + 2 * i, (u16)(mii[0x1A + 2 * i] | mii[0x1B + 2 * i] << 8));
            break;
        }
        case 0x000A: {  // GetMyMii: the player's Mii, 0x60 bytes with its checksum
            const auto mii = player_mii();
            ipc.ok(0x19);
            memcpy(gptr(ipc.buf + 8), mii.data(), 0x60);
            break;
        }
        case 0x000B:  // GetMyLocalAccountId
            ipc.ok(2);
            ipc.set(2, 1);
            break;
        case 0x0011:  // GetFriendKeyList
            ipc.ok(2, 2);
            ipc.set(2, 0);
            ipc.static_desc(3, 0, 0);
            break;
        default:
            unimplemented(ipc);
        }
    }
};
#endif

struct Ndm : Service {
    using Service::Service;
    void handle(IPC &ipc) override {
        switch (ipc.cmd()) {
        case 0x000D:  // GetCurrentState
        case 0x000E:  // QueryStatus
            ipc.ok(2);
            ipc.set(2, 0);
            break;
        default:
            ipc.ok();
        }
    }
};

struct Cecd : Service {
    using Service::Service;
    std::shared_ptr<Event> info = make_event(ONESHOT, "cecd info");
    std::shared_ptr<Event> state = make_event(ONESHOT, "cecd state");
    void handle(IPC &ipc) override {
        switch (ipc.cmd()) {
        case 0x000F:  // GetCecInfoEventHandle
            ipc.ok(1, 2);
            ipc.handles(2, {info});
            break;
        case 0x0010:  // GetChangeStateEventHandle
            ipc.ok(1, 2);
            ipc.handles(2, {state});
            break;
        case 0x000E:  // GetCecStateAbbreviated
            ipc.ok(2);
            ipc.set(2, 1);  // idle
            break;
        case 0x0001:  // Open -> fails: no message box
        case 0x0002:
            ipc.result(0xC8A11BFA, 2);
            ipc.set(2, 0);
            break;
        // Kid Icarus: Uprising reads and writes its StreetPass box at start-up; answered as "unimplemented" (the request's own words back), it
        // stopped drawing for good (2026-11-08). Nothing to read, writes taken, the mapped buffer handed back as Azahar does.
        case 0x0012: {  // OpenAndRead(size, program id, path type, ncch id, pid, buffer) -> nothing there
            const u32 desc = ipc[7], addr = ipc[8];
            ipc.result(0xC8A11BFA, 2, 2);
            ipc.set(2, 0);
            ipc.set(3, desc);
            ipc.set(4, addr);
            break;
        }
        case 0x0011: {  // OpenAndWrite(size, program id, path type, ncch id, pid, buffer)
            const u32 desc = ipc[7], addr = ipc[8];
            ipc.ok(1, 2);
            ipc.set(2, desc);
            ipc.set(3, addr);
            break;
        }
        case 0x0009: {  // SetData(program id, size, type, buffer)
            const u32 desc = ipc[4], addr = ipc[5];
            ipc.ok(1, 2);
            ipc.set(2, desc);
            ipc.set(3, addr);
            break;
        }
        case 0x0003:  // ReadMessage
        case 0x0004:  // ReadMessageWithHMAC: no messages
            echo(ipc, 0xC8A11BFA, true);
            break;
        default:  // Write, WriteMessage(WithHMAC), Delete (Super Mario 3D Land crashed after its answer was the request read back), ReadData,
                  // Start, Stop, GetCecInfoBuffer, ...: done, nothing to say
            echo(ipc, 0, false);
        }
    }
    // the result (and a zero size), then the request's buffer descriptors handed back, as Azahar answers: never the request's own words
    static void echo(IPC &ipc, u32 result, bool with_size) {
        const u32 hdr = ipc[0], n = (hdr >> 6) & 63, t = hdr & 63;
        u32 tw[16], k = 0;
        for (u32 i = 0; i < t && k + 1 < 16; i++) {
            const u32 d = ipc[1 + n + i];
            if (d == 0x20) {  // the calling process's id: not part of an answer
                i++;
                continue;
            }
            tw[k++] = d;
        }
        const u32 normal = with_size ? 2 : 1;
        ipc.result(result, normal, k);
        if (with_size) ipc.set(2, 0);
        for (u32 i = 0; i < k; i++) ipc.set(1 + normal + i, tw[i]);
    }
};

struct Boss : Service {
    using Service::Service;
    void handle(IPC &ipc) override {
        switch (ipc.cmd()) {
        case 0x0001:  // InitializeSession
        case 0x0002:  // SetStorageInfo
        case 0x0003:  // UnregisterStorage
            ipc.ok();
            break;
        case 0x0004:  // GetStorageInfo
            ipc.ok(2);
            ipc.set(2, 0);
            break;
        case 0x0006:  // GetOptoutFlag
            ipc.ok(2);
            ipc.set(2, 0);
            break;
        case 0x000D: {  // GetTaskIdList
            ipc.ok();
            break;
        }
        default:
            unimplemented(ipc);
        }
    }
};

// The game's microphone buffer is a block of its own memory. Samples arrive in 10 ms batches; the last word of the block says how far
// they have got (3dbrew: MIC Shared Memory).
struct Mic : Service {
    using Service::Service;
    std::shared_ptr<Event> buf_full = make_event(ONESHOT, "mic");
    std::shared_ptr<SharedMem> mem;
    u32 start = 0, end = 0, pos = 0, rate = 32730, generation = 0;
    u8 encoding = 0, gain = 0, power = 0, clamp = 0;
    bool loop = false, sampling = false;

    void stop() {
        if (sampling) mic_host_stop();
        sampling = false;
        generation++;
    }
    void store(u32 a, s16 s) {
        switch (encoding) {
        case 0: *(u8 *)gptr(a) = (u8)((s >> 8) + 128); break;  // unsigned 8-bit
        case 1: w16(a, (u16)(s + 32768)); break;               // unsigned 16-bit
        case 2: *(u8 *)gptr(a) = (u8)(s >> 8); break;          // signed 8-bit
        default: w16(a, (u16)s);                               // signed 16-bit
        }
    }
    void tick(u32 g) {
        if (g != generation || !sampling) return;
        s16 in[512];
        const u32 n = rate / 100, step = encoding & 1 ? 2 : 1;
        mic_host_read(in, n, rate);
        for (u32 i = 0; i < n && sampling; i++) {
            if (pos + step > end) {
                buf_full->signal();
                if (!loop) {
                    stop();
                    break;
                }
                pos = start;
            }
            store(mem->backing + pos, in[i]);
            pos += step;
        }
        w32(mem->backing + mem->size - 4, pos);
        if (sampling) add_timeout(now_ns() + 10000000, [this, g] { tick(g); });
    }
    void handle(IPC &ipc) override {
        static const u32 rates[4] = {32730, 16360, 10910, 8180};
        switch (ipc.cmd()) {
        case 0x0001:  // MapSharedMemory(size, handle)
            mem = get<SharedMem>(ipc[3]);
            ipc.ok();
            break;
        case 0x0002:  // UnmapSharedMemory
            stop();
            mem = nullptr;
            ipc.ok();
            break;
        case 0x0003:  // StartSampling(encoding, rate, offset, size, loop)
            stop();
            if (mem) {
                encoding = (u8)ipc[1] & 3;
                rate = rates[ipc[2] & 3];
                start = pos = ipc[3];
                end = std::min(start + ipc[4], mem->size - 4);
                loop = ipc[5] & 1;
                sampling = true;
                LOG("[mic] sampling at %u Hz, encoding %u, %u bytes%s\n", rate, encoding, end - start, loop ? ", looped" : "");
                mic_host_start();
                const u32 g = generation;
                add_timeout(now_ns() + 10000000, [this, g] { tick(g); });
            }
            ipc.ok();
            break;
        case 0x0004:  // AdjustSampling(rate)
            rate = rates[ipc[1] & 3];
            ipc.ok();
            break;
        case 0x0005:  // StopSampling
            stop();
            ipc.ok();
            break;
        case 0x0006:  // IsSampling
            ipc.ok(2);
            ipc.set(2, sampling);
            break;
        case 0x0007:  // GetBufferFullEvent
            ipc.ok(1, 2);
            ipc.handles(2, {buf_full});
            break;
        case 0x0008:  // SetGain
            gain = (u8)ipc[1];
            ipc.ok();
            break;
        case 0x0009:  // GetGain
            ipc.ok(2);
            ipc.set(2, gain);
            break;
        case 0x000A:  // SetPower
            power = (u8)ipc[1];
            ipc.ok();
            break;
        case 0x000B:  // GetPower
            ipc.ok(2);
            ipc.set(2, power);
            break;
        case 0x000D:  // SetClamp
            clamp = (u8)ipc[1];
            ipc.ok();
            break;
        case 0x000E:  // GetClamp
            ipc.ok(2);
            ipc.set(2, clamp);
            break;
        default:  // the filter, SetAllowShellClosed, SetClientVersion
            ipc.ok();
        }
    }
};

struct Ir : Service {
    using Service::Service;
    std::shared_ptr<Event> conn = make_event(ONESHOT, "ir conn");
    std::shared_ptr<Event> recv = make_event(ONESHOT, "ir recv");
    std::shared_ptr<Event> send = make_event(ONESHOT, "ir send");
    void handle(IPC &ipc) override {
        switch (ipc.cmd()) {
        case 0x000C:  // GetConnectionStatusEvent
            ipc.ok(1, 2);
            ipc.handles(2, {conn});
            break;
        case 0x000D:  // GetReceiveEvent
            ipc.ok(1, 2);
            ipc.handles(2, {recv});
            break;
        case 0x000E:  // GetSendEvent
            ipc.ok(1, 2);
            ipc.handles(2, {send});
            break;
        default:
            ipc.ok();
        }
    }
};

struct Uds;
static Uds *g_uds_instance = nullptr;
static std::atomic<bool> g_uds_active{false};

// Local wireless over LAN/Wi-Fi UDP (Wii U <-> Wii U).
// Implements 3DS nwm::UDS discovery beacons, node management, and data channels over UDP port 28340.
struct Uds : Service {
    using Service::Service;
    using Clock = std::chrono::steady_clock;

    std::shared_ptr<Event> status = make_event(ONESHOT, "uds status");
    uds_lan::UdpTransport udp;
    Clock::time_point last_beacon{};

    bool initialized = false;
    u32 conn_status = (u32)uds_lan::NetworkStatus::NotConnected;
    u32 disconnect_reason = (u32)uds_lan::DisconnectReason::NotConnected;
    u16 my_node_id = 0;
    u16 changed_nodes = 0;
    u16 node_bitmask = 0;
    u8 total_nodes = 0;
    u16 nodes_ids[uds_lan::kUdsMaxNodes]{};
    std::array<uds_lan::NodeInfo, uds_lan::kUdsMaxNodes> node_infos{};
    uds_lan::NodeInfo my_node{};
    uds_lan::NetworkInfo net_info{};
    std::vector<u8> host_passphrase;

    struct RemoteBeacon {
        uds_lan::NetworkInfo net;
        std::array<uds_lan::NodeInfo, uds_lan::kUdsMaxNodes> nodes{};
        u16 bitmask = 0;
        u32 ip_be = 0;
        u16 port_be = 0;
        Clock::time_point seen{};
    };
    std::vector<RemoteBeacon> beacons;

    struct PeerEndpoint {
        u16 node_id = 0;
        u32 ip_be = 0;
        u16 port_be = 0;
        std::array<u8, 6> mac{};
    };
    std::vector<PeerEndpoint> peers;

    struct RxPacket {
        u16 src_node_id = 0;
        std::vector<u8> data;
    };
    struct BindEntry {
        u32 bind_node_id = 0;
        u8 data_channel = 0;
        u16 filter_node_id = 0;
        std::shared_ptr<Event> event;
        std::deque<RxPacket> queue;
    };
    std::vector<BindEntry> binds;

    bool join_pending = false;
    bool join_done = false;
    bool join_accepted = false;
    u16 tx_seq = 0;

    ~Uds() {
        g_uds_active.store(false);
        if (g_uds_instance == this) g_uds_instance = nullptr;
        udp.close();
    }

    static constexpr u8 kMsgBeacon = 1;
    static constexpr u8 kMsgJoinReq = 2;
    static constexpr u8 kMsgJoinRsp = 3;
    static constexpr u8 kMsgNodeMap = 4;
    static constexpr u8 kMsgData = 5;
    static constexpr u8 kMsgLeave = 6;

    std::vector<u8> make_udp_packet(u8 msg_type, u16 sender_node, const u8 *payload, u16 plen) const {
        std::vector<u8> pkt(24 + plen);
        pkt[0] = 'U'; pkt[1] = 'D'; pkt[2] = 'S'; pkt[3] = '1';
        pkt[4] = msg_type;
        pkt[5] = 0;
        uds_lan::wbe16(&pkt[6], sender_node);
        uds_lan::wbe32(&pkt[8], net_info.network_id());
        uds_lan::wbe32(&pkt[12], net_info.wlan_comm_id());
        std::memcpy(&pkt[16], udp.local_mac().data(), 6);
        uds_lan::wbe16(&pkt[22], plen);
        if (plen && payload) std::memcpy(&pkt[24], payload, plen);
        return pkt;
    }

    std::vector<u8> build_beacon_udp_packet() const {
        if (conn_status != (u32)uds_lan::NetworkStatus::ConnectedAsHost) return {};
        std::vector<u8> pl(0x108 + 2 + uds_lan::kUdsMaxNodes * 40);
        std::memcpy(&pl[0], net_info.raw.data(), 0x108);
        uds_lan::wbe16(&pl[0x108], node_bitmask);
        for (u32 i = 0; i < uds_lan::kUdsMaxNodes; i++)
            node_infos[i].serialize_le(&pl[0x10A + i * 40]);
        return make_udp_packet(kMsgBeacon, my_node_id, pl.data(), (u16)pl.size());
    }

    void broadcast_beacon() {
        if (!udp.ready()) return;
        auto pkt = build_beacon_udp_packet();
        if (!pkt.empty()) udp.send_broadcast(pkt.data(), pkt.size());
    }

    void send_node_map() {
        if (conn_status != (u32)uds_lan::NetworkStatus::ConnectedAsHost || !udp.ready()) return;
        // Payload: bitmask(2) + total_nodes(1) + max_nodes(1) + nodes_ids[16](32) + node_infos[16](640) + peers_ep[16](16*6=96)
        std::vector<u8> pl(4 + 32 + 640 + 96, 0);
        uds_lan::wbe16(&pl[0], node_bitmask);
        pl[2] = total_nodes;
        pl[3] = net_info.max_nodes();
        for (u32 i = 0; i < uds_lan::kUdsMaxNodes; i++) uds_lan::wbe16(&pl[4 + i * 2], nodes_ids[i]);
        for (u32 i = 0; i < uds_lan::kUdsMaxNodes; i++) node_infos[i].serialize_le(&pl[36 + i * 40]);
        for (const auto &p : peers) {
            if (p.node_id >= 1 && p.node_id <= uds_lan::kUdsMaxNodes) {
                size_t off = 676 + (size_t)(p.node_id - 1) * 6;
                std::memcpy(&pl[off], &p.ip_be, 4);
                std::memcpy(&pl[off + 4], &p.port_be, 2);
            }
        }
        auto pkt = make_udp_packet(kMsgNodeMap, my_node_id, pl.data(), (u16)pl.size());
        for (const auto &p : peers) udp.send_unicast(p.ip_be, p.port_be, pkt.data(), pkt.size());
    }

    void apply_node_map(const u8 *pl, size_t plen, u32 host_ip_be, u16 host_port_be) {
        if (plen < 676) return;
        u16 old_mask = node_bitmask;
        node_bitmask = uds_lan::rbe16(&pl[0]);
        total_nodes = pl[2];
        net_info.set_total_nodes(total_nodes);
        for (u32 i = 0; i < uds_lan::kUdsMaxNodes; i++) nodes_ids[i] = uds_lan::rbe16(&pl[4 + i * 2]);
        for (u32 i = 0; i < uds_lan::kUdsMaxNodes; i++) node_infos[i].deserialize_le(&pl[36 + i * 40]);
        if (plen >= 772) {
            peers.clear();
            // Always keep host endpoint
            PeerEndpoint hep{};
            hep.node_id = 1; hep.ip_be = host_ip_be; hep.port_be = host_port_be;
            net_info.get_host_mac(hep.mac.data());
            peers.push_back(hep);
            for (u32 i = 1; i < uds_lan::kUdsMaxNodes; i++) {
                u16 nid = nodes_ids[i];
                if (!nid || nid == my_node_id) continue;
                size_t off = 676 + (size_t)(nid - 1) * 6;
                u32 ip = 0; u16 pt = 0;
                std::memcpy(&ip, &pl[off], 4);
                std::memcpy(&pt, &pl[off + 4], 2);
                if (ip) peers.push_back({nid, ip, pt, {}});
            }
        }
        changed_nodes |= (u16)(old_mask ^ node_bitmask);
        if (changed_nodes) status->signal();
    }

    void reset_network_state(u32 new_status, u32 new_reason) {
        conn_status = new_status;
        disconnect_reason = new_reason;
        my_node_id = 0;
        changed_nodes = 0;
        node_bitmask = 0;
        total_nodes = 0;
        std::memset(nodes_ids, 0, sizeof(nodes_ids));
        for (auto &n : node_infos) n.clear();
        peers.clear();
        for (auto &b : binds) {
            b.queue.clear();
            if (b.event) b.event->signal();
        }
        status->signal();
    }

    void handle_udp_packet(const u8 *buf, int len, u32 src_ip_be, u16 src_port_be) {
        if (len < 24 || buf[0] != 'U' || buf[1] != 'D' || buf[2] != 'S' || buf[3] != '1') return;
        if (std::memcmp(&buf[16], udp.local_mac().data(), 6) == 0) return;  // ignore self
        u8 msg_type = buf[4];
        u16 sender_node = uds_lan::rbe16(&buf[6]);
        u32 net_id = uds_lan::rbe32(&buf[8]);
        u32 comm_id = uds_lan::rbe32(&buf[12]);
        u16 plen = uds_lan::rbe16(&buf[22]);
        if (24 + (int)plen > len) return;
        const u8 *pl = buf + 24;

        switch (msg_type) {
        case kMsgBeacon: {
            if (plen < 0x10A + uds_lan::kUdsMaxNodes * 40) break;
            uds_lan::NetworkInfo ni{};
            std::memcpy(ni.raw.data(), pl, 0x108);
            u16 bmask = uds_lan::rbe16(&pl[0x108]);
            auto now = Clock::now();
            u8 hmac[6]; ni.get_host_mac(hmac);
            auto it = std::find_if(beacons.begin(), beacons.end(), [&](const RemoteBeacon &rb) {
                u8 m[6]; rb.net.get_host_mac(m);
                return std::memcmp(m, hmac, 6) == 0;
            });
            if (it == beacons.end()) {
                beacons.emplace_back();
                it = beacons.end() - 1;
            }
            it->net = ni;
            it->bitmask = bmask;
            it->ip_be = src_ip_be;
            it->port_be = src_port_be;
            it->seen = now;
            for (u32 i = 0; i < uds_lan::kUdsMaxNodes; i++)
                it->nodes[i].deserialize_le(&pl[0x10A + i * 40]);
            break;
        }
        case kMsgJoinReq: {
            if (conn_status != (u32)uds_lan::NetworkStatus::ConnectedAsHost) break;
            if (net_id != net_info.network_id() || comm_id != net_info.wlan_comm_id()) break;
            if (plen < 42) break;
            uds_lan::NodeInfo cnode{};
            cnode.deserialize_le(pl + 1);
            u8 pass_len = pl[41];
            bool pass_ok = (plen >= 42 + pass_len) &&
                (host_passphrase.empty() || (pass_len == host_passphrase.size() && std::memcmp(pl + 42, host_passphrase.data(), pass_len) == 0));

            u16 assigned = 0;
            int free_slot = -1;
            if (pass_ok) {
                // Check if this MAC is already connected
                for (const auto &p : peers) {
                    if (std::memcmp(p.mac.data(), &buf[16], 6) == 0) {
                        assigned = p.node_id;
                        free_slot = (int)assigned - 1;
                        break;
                    }
                }
                if (free_slot < 0) {
                    u8 max_n = net_info.max_nodes();
                    for (int i = 1; i < (int)max_n; i++) {
                        if (nodes_ids[i] == 0) { free_slot = i; assigned = (u16)(i + 1); break; }
                    }
                }
            }

            if (free_slot >= 0 && assigned != 0) {
                bool is_new = (nodes_ids[free_slot] == 0);
                cnode.network_node_id = assigned;
                nodes_ids[free_slot] = assigned;
                node_infos[free_slot] = cnode;
                node_bitmask |= (u16)(1u << free_slot);
                if (is_new) {
                    total_nodes++;
                    net_info.set_total_nodes(total_nodes);
                    PeerEndpoint pe{};
                    pe.node_id = assigned; pe.ip_be = src_ip_be; pe.port_be = src_port_be;
                    std::memcpy(pe.mac.data(), &buf[16], 6);
                    peers.push_back(pe);
                    changed_nodes |= (u16)(1u << free_slot);
                }
                // Send JOIN_RSP (accepted = 1)
                std::vector<u8> rsp(3 + 4 + 32 + 640, 0);
                rsp[0] = 1;
                uds_lan::wbe16(&rsp[1], assigned);
                uds_lan::wbe16(&rsp[3], node_bitmask);
                rsp[5] = total_nodes;
                rsp[6] = net_info.max_nodes();
                for (u32 i = 0; i < uds_lan::kUdsMaxNodes; i++) uds_lan::wbe16(&rsp[7 + i * 2], nodes_ids[i]);
                for (u32 i = 0; i < uds_lan::kUdsMaxNodes; i++) node_infos[i].serialize_le(&rsp[39 + i * 40]);
                auto pkt = make_udp_packet(kMsgJoinRsp, my_node_id, rsp.data(), (u16)rsp.size());
                udp.send_unicast(src_ip_be, src_port_be, pkt.data(), pkt.size());
                send_node_map();
                broadcast_beacon();
                status->signal();
            } else {
                u8 rsp[3] = {0, 0, 0};
                auto pkt = make_udp_packet(kMsgJoinRsp, my_node_id, rsp, sizeof(rsp));
                udp.send_unicast(src_ip_be, src_port_be, pkt.data(), pkt.size());
            }
            break;
        }
        case kMsgJoinRsp: {
            if (!join_pending || plen < 3) break;
            join_done = true;
            join_accepted = (pl[0] == 1);
            if (join_accepted && plen >= 679) {
                my_node_id = uds_lan::rbe16(&pl[1]);
                my_node.network_node_id = my_node_id;
                apply_node_map(pl + 3, (size_t)(plen - 3), src_ip_be, src_port_be);
            }
            break;
        }
        case kMsgNodeMap: {
            if (conn_status == (u32)uds_lan::NetworkStatus::ConnectedAsClient ||
                conn_status == (u32)uds_lan::NetworkStatus::ConnectedAsSpectator) {
                if (net_id == net_info.network_id()) apply_node_map(pl, plen, src_ip_be, src_port_be);
            }
            break;
        }
        case kMsgData: {
            if (conn_status != (u32)uds_lan::NetworkStatus::ConnectedAsHost &&
                conn_status != (u32)uds_lan::NetworkStatus::ConnectedAsClient &&
                conn_status != (u32)uds_lan::NetworkStatus::ConnectedAsSpectator) break;
            if (net_id != net_info.network_id() || plen < 8) break;
            u16 dest_node = uds_lan::rbe16(&pl[0]);
            u8 dchan = pl[2];
            u8 flags = pl[3];
            u16 dsize = uds_lan::rbe16(&pl[6]);
            if (8 + (size_t)dsize > plen) break;
            bool for_me = (dest_node == 0xFFFF || dest_node == my_node_id || (flags & 2));
            if (!for_me) break;
            for (auto &b : binds) {
                if (b.data_channel == dchan &&
                    (b.filter_node_id == 0 || b.filter_node_id == 0xFFFF || b.filter_node_id == sender_node)) {
                    RxPacket rx;
                    rx.src_node_id = sender_node;
                    rx.data.assign(pl + 8, pl + 8 + dsize);
                    b.queue.push_back(std::move(rx));
                    if (b.event) b.event->signal();
                }
            }
            break;
        }
        case kMsgLeave: {
            if (net_id != net_info.network_id()) break;
            if (conn_status == (u32)uds_lan::NetworkStatus::ConnectedAsHost) {
                for (u32 i = 1; i < uds_lan::kUdsMaxNodes; i++) {
                    if (nodes_ids[i] == sender_node) {
                        nodes_ids[i] = 0;
                        node_infos[i].clear();
                        node_bitmask &= ~(u16)(1u << i);
                        if (total_nodes > 1) total_nodes--;
                        net_info.set_total_nodes(total_nodes);
                        changed_nodes |= (u16)(1u << i);
                    }
                }
                peers.erase(std::remove_if(peers.begin(), peers.end(),
                    [&](const PeerEndpoint &p) { return p.node_id == sender_node; }), peers.end());
                send_node_map();
                broadcast_beacon();
                status->signal();
            } else if ((conn_status == (u32)uds_lan::NetworkStatus::ConnectedAsClient ||
                        conn_status == (u32)uds_lan::NetworkStatus::ConnectedAsSpectator) && sender_node == 1) {
                reset_network_state((u32)uds_lan::NetworkStatus::NotConnected, (u32)uds_lan::DisconnectReason::ConnectionLost);
            }
            break;
        }
        }
    }

    void poll_udp_locked() {
        u8 buf[2048];
        u32 sip = 0; u16 spt = 0;
        for (int iter = 0; iter < 32; iter++) {
            int n = udp.recv(buf, sizeof(buf), sip, spt);
            if (n <= 0) break;
            handle_udp_packet(buf, n, sip, spt);
        }
    }

    void tick_locked() {
        if (!initialized) return;
        poll_udp_locked();
        auto now = Clock::now();
        if (now - last_beacon >= std::chrono::milliseconds(100)) {
            last_beacon = now;
            if (conn_status == (u32)uds_lan::NetworkStatus::ConnectedAsHost && udp.ready())
                broadcast_beacon();
            beacons.erase(std::remove_if(beacons.begin(), beacons.end(), [&](const RemoteBeacon &rb) {
                return (now - rb.seen) > std::chrono::seconds(3);
            }), beacons.end());
        }
    }

    void ensure_udp(Lock *lk) {
        if (udp.ready()) return;
        if (lk) run_blocked(*lk, [&] { udp.init(); });
        else udp.init();
        if (udp.ready()) {
            last_beacon = Clock::now();
            g_uds_instance = this;
            g_uds_active.store(true);
        }
    }

    void handle(IPC &ipc) override {
        switch (ipc.cmd()) {
        case 0x0001:  // InitializeDeprecated
        case 0x001B:  // InitializeWithVersion
            my_node.from_ipc(ipc, 2);
            initialized = true;
            beacons.clear();
            binds.clear();
            reset_network_state((u32)uds_lan::NetworkStatus::NotConnected, (u32)uds_lan::DisconnectReason::NotConnected);
            status->clear();
            ipc.ok(1, 2);
            ipc.handles(2, {status});
            break;

        case 0x0004:  // BeginHostingNetworkDeprecated
        case 0x001D: {  // BeginHostingNetwork
            ensure_udp(ipc.lk);
            std::memset(net_info.raw.data(), 0, net_info.raw.size());
            u32 pass_sz = 0, pass_va = 0;
            if (ipc.cmd() == 0x001D) {
                pass_sz = ipc[1];
                u32 ni_va = ipc[3];
                pass_va = ipc[5];
                if (ni_va) std::memcpy(net_info.raw.data(), gptr(ni_va), 0x108);
            } else {
                for (int i = 0; i < 15; i++) uds_lan::wle32(&net_info.raw[i * 4], ipc[1 + i]);
                pass_sz = ipc[16];
                pass_va = ipc[18];
            }
            host_passphrase.clear();
            if (pass_sz && pass_va) {
                const u8 *p = gptr(pass_va);
                host_passphrase.assign(p, p + std::min<u32>(pass_sz, 256));
            }
            net_info.set_host_mac(udp.local_mac().data());
            net_info.set_channel(11);
            net_info.set_oui_nintendo();
            if (net_info.network_id() == 0) net_info.set_network_id((u32)(std::time(nullptr) ^ 0x3D110001u));

            peers.clear();
            std::memset(nodes_ids, 0, sizeof(nodes_ids));
            for (auto &n : node_infos) n.clear();
            my_node_id = 1;
            my_node.network_node_id = 1;
            nodes_ids[0] = 1;
            node_infos[0] = my_node;
            node_bitmask = 1;
            total_nodes = 1;
            net_info.set_total_nodes(1);
            conn_status = (u32)uds_lan::NetworkStatus::ConnectedAsHost;
            disconnect_reason = (u32)uds_lan::DisconnectReason::None;
            changed_nodes = 1;
            broadcast_beacon();
            status->signal();
            ipc.ok();
            break;
        }

        case 0x0007:  // UpdateNetworkAttribute
            net_info.set_attributes((u16)ipc[1]);
            broadcast_beacon();
            ipc.ok();
            break;

        case 0x0010: {  // SetApplicationData
            u32 sz = ipc[1], va = ipc[3];
            net_info.set_app_data(va ? gptr(va) : nullptr, (u8)std::min<u32>(sz, uds_lan::kAppDataMax));
            broadcast_beacon();
            ipc.ok();
            break;
        }

        case 0x0011: {  // GetApplicationData
            u32 bsz = 0;
            u32 dst = ipc.static_buffer(0, &bsz);
            u32 sz = std::min<u32>({ipc[1], bsz, net_info.app_data_size()});
            if (dst && sz) std::memcpy(gptr(dst), &net_info.raw[0x40], sz);
            ipc.ok(2, 2);
            ipc.set(2, sz);
            ipc.static_desc(3, 0, sz);
            break;
        }

        case 0x000B:  // GetConnectionStatus
            ipc.ok(13);
            ipc.set(2, conn_status);
            ipc.set(3, disconnect_reason);
            ipc.set(4, (u32)my_node_id | ((u32)changed_nodes << 16));
            changed_nodes = 0;
            for (int i = 0; i < 8; i++)
                ipc.set(5 + i, (u32)nodes_ids[i * 2] | ((u32)nodes_ids[i * 2 + 1] << 16));
            ipc.set(13, (u32)total_nodes | ((u32)net_info.max_nodes() << 8) | ((u32)node_bitmask << 16));
            break;

        case 0x000D: {  // GetNodeInformation
            u16 target = (u16)ipc[1];
            bool found = false;
            for (u32 i = 0; i < uds_lan::kUdsMaxNodes; i++) {
                if (nodes_ids[i] == target && node_infos[i].network_node_id == target) {
                    ipc.ok(11);
                    node_infos[i].to_ipc(ipc, 2);
                    found = true;
                    break;
                }
            }
            if (!found) ipc.result(0xC90113FA);
            break;
        }

        case 0x000F: {  // RecvBeaconBroadcastData
            ensure_udp(ipc.lk);
            poll_udp_locked();
            const u32 out_max = ipc[1];
            u8 filter_mac[6]{};
            uds_lan::wle32(filter_mac, ipc[4]);
            uds_lan::wle16(filter_mac + 4, (u16)ipc[5]);
            bool any_mac = true;
            bool all_ff = true, all_00 = true;
            for (int i = 0; i < 6; i++) {
                if (filter_mac[i] != 0xFF) all_ff = false;
                if (filter_mac[i] != 0x00) all_00 = false;
            }
            any_mac = all_ff || all_00;

            const u32 desc = ipc[19], buf = ipc[20];
            u32 cur = 12, entries = 0;
            auto now = Clock::now();
            for (const auto &rb : beacons) {
                if ((now - rb.seen) > std::chrono::seconds(3)) continue;
                u8 hmac[6]; rb.net.get_host_mac(hmac);
                if (!any_mac && std::memcmp(hmac, filter_mac, 6) != 0) continue;
                auto frame = uds_lan::build_beacon_frame(rb.net, rb.nodes, rb.bitmask);
                u32 entry_sz = 0x1C + (u32)frame.size();
                if (cur + entry_sz > out_max) break;
                u8 *dst = gptr(buf + cur);
                std::memset(dst, 0, 0x1C);
                uds_lan::wle32(dst + 0x00, entry_sz);
                dst[0x05] = rb.net.channel() ? rb.net.channel() : 11;
                std::memcpy(dst + 0x08, hmac, 6);
                uds_lan::wle32(dst + 0x14, entry_sz);
                uds_lan::wle32(dst + 0x18, 0x1C);
                std::memcpy(dst + 0x1C, frame.data(), frame.size());
                cur += entry_sz;
                entries++;
            }
            w32(buf + 0, out_max);
            w32(buf + 4, cur);
            w32(buf + 8, entries);
            ipc.ok(1, 2);
            ipc.set(2, desc);
            ipc.set(3, buf);
            break;
        }

        case 0x000E:  // DecryptBeaconDataDeprecated
        case 0x001F: {  // DecryptBeaconData
            u32 nsz = ipc[1] >> 14, nva = ipc[2];
            u32 e0sz = ipc[3] >> 14, e0va = ipc[4];
            u32 e1sz = ipc[5] >> 14, e1va = ipc[6];
            if (!nva || nsz < 0x108 || !e0va || e0sz <= 4) {
                ipc.result(0xE0E113F8);
                break;
            }
            std::vector<u8> raw;
            const u8 *e0 = gptr(e0va);
            raw.insert(raw.end(), e0 + 4, e0 + e0sz);
            if (e1va && e1sz > 4) {
                const u8 *e1 = gptr(e1va);
                raw.insert(raw.end(), e1 + 4, e1 + e1sz);
            }
            uds_lan::NetworkInfo ni{};
            std::memcpy(ni.raw.data(), gptr(nva), 0x108);
            u8 max_n = ni.max_nodes();

            std::array<uds_lan::NodeInfo, uds_lan::kUdsMaxNodes> out_nodes{};
            for (u8 i = 0; i < max_n; i++) {
                size_t off = 18 + (size_t)i * 30;
                if (off + 30 > raw.size()) break;
                const u8 *bn = &raw[off];
                out_nodes[i].friend_code_seed = uds_lan::rbe64(bn + 0);
                for (int j = 0; j < 10; j++) out_nodes[i].username[j] = uds_lan::rbe16(bn + 8 + j * 2);
                out_nodes[i].network_node_id = uds_lan::rbe16(bn + 28);
            }
            u32 out_sz = uds_lan::kUdsMaxNodes * 40;
            u32 out_cap = 0;
            u32 out_va = ipc.static_buffer(0, &out_cap);
            if (out_va && out_cap >= out_sz) {
                u8 *p = gptr(out_va);
                for (u32 i = 0; i < uds_lan::kUdsMaxNodes; i++) out_nodes[i].serialize_le(p + i * 40);
                ipc.ok(1, 2);
                ipc.static_desc(2, 0, out_sz);
            } else {
                ipc.result(0xE0E113F8);
            }
            break;
        }

        case 0x0009:  // ConnectToNetworkDeprecated
        case 0x001E: {  // ConnectToNetwork
            ensure_udp(ipc.lk);
            std::memset(net_info.raw.data(), 0, net_info.raw.size());
            u8 conn_type = 1;
            u32 pass_sz = 0, pass_va = 0;
            if (ipc.cmd() == 0x001E) {
                conn_type = (u8)ipc[1];
                pass_sz = ipc[2];
                u32 ni_va = ipc[4];
                pass_va = ipc[6];
                if (ni_va) std::memcpy(net_info.raw.data(), gptr(ni_va), 0x108);
            } else {
                for (int i = 0; i < 15; i++) uds_lan::wle32(&net_info.raw[i * 4], ipc[1 + i]);
                conn_type = (u8)ipc[16];
                pass_sz = ipc[17];
                pass_va = ipc[19];
            }
            u8 hmac[6]; net_info.get_host_mac(hmac);
            auto it = std::find_if(beacons.begin(), beacons.end(), [&](const RemoteBeacon &rb) {
                u8 m[6]; rb.net.get_host_mac(m);
                return std::memcmp(m, hmac, 6) == 0 || (rb.net.network_id() == net_info.network_id() && net_info.network_id() != 0);
            });
            if (it == beacons.end() || !udp.ready()) {
                status->signal();
                ipc.result(0xE0E113F8);
                break;
            }
            net_info = it->net;
            u32 host_ip = it->ip_be;
            u16 host_port = it->port_be;

            u8 plen_clamped = (u8)std::min<u32>(pass_sz, 200);
            std::vector<u8> req(42 + plen_clamped, 0);
            req[0] = conn_type;
            my_node.serialize_le(&req[1]);
            req[41] = plen_clamped;
            if (plen_clamped && pass_va) std::memcpy(&req[42], gptr(pass_va), plen_clamped);

            join_pending = true;
            join_done = false;
            join_accepted = false;
            auto pkt = make_udp_packet(kMsgJoinReq, 0, req.data(), (u16)req.size());
            udp.send_unicast(host_ip, host_port, pkt.data(), pkt.size());

            for (int w = 0; w < 250 && !join_done; w++) {
                if (ipc.lk) run_blocked(*ipc.lk, [] { std::this_thread::sleep_for(std::chrono::milliseconds(4)); });
                else std::this_thread::sleep_for(std::chrono::milliseconds(4));
                poll_udp_locked();
                if (w == 50 || w == 125) udp.send_unicast(host_ip, host_port, pkt.data(), pkt.size());
            }
            join_pending = false;
            if (join_done && join_accepted) {
                conn_status = (conn_type == 2) ? (u32)uds_lan::NetworkStatus::ConnectedAsSpectator
                                               : (u32)uds_lan::NetworkStatus::ConnectedAsClient;
                disconnect_reason = (u32)uds_lan::DisconnectReason::None;
                changed_nodes = node_bitmask;
                status->signal();
                ipc.ok();
            } else {
                reset_network_state((u32)uds_lan::NetworkStatus::NotConnected, (u32)uds_lan::DisconnectReason::NotConnected);
                ipc.result(0xE0E113F8);
            }
            break;
        }

        case 0x0012: {  // Bind
            u32 bnode = ipc[1];
            u8 dchan = (u8)ipc[3];
            u16 fnode = (u16)ipc[4];
            auto it = std::find_if(binds.begin(), binds.end(), [&](const BindEntry &b) { return b.bind_node_id == bnode; });
            if (it == binds.end()) {
                BindEntry be;
                be.bind_node_id = bnode;
                be.data_channel = dchan;
                be.filter_node_id = fnode;
                be.event = make_event(ONESHOT, "uds bind");
                binds.push_back(std::move(be));
                it = binds.end() - 1;
            }
            ipc.ok(1, 2);
            ipc.handles(2, {it->event});
            break;
        }

        case 0x0013: {  // Unbind
            u32 bnode = ipc[1];
            for (auto it = binds.begin(); it != binds.end(); ++it) {
                if (it->bind_node_id == bnode) {
                    if (it->event) it->event->signal();
                    binds.erase(it);
                    break;
                }
            }
            ipc.ok();
            break;
        }

        case 0x0017: {  // SendTo
            u16 dest_node = (u16)ipc[2];
            u8 dchan = (u8)ipc[3];
            u32 dsize = ipc[5];
            u8 flags = (u8)ipc[6];
            u32 bsz = ipc[7] >> 14;
            u32 src = ipc[8];
            if (!src || dsize > bsz || dsize > uds_lan::kMaxDataPayload) {
                ipc.result(0xE0E113F8);
                break;
            }
            if (conn_status != (u32)uds_lan::NetworkStatus::ConnectedAsHost &&
                conn_status != (u32)uds_lan::NetworkStatus::ConnectedAsClient) {
                ipc.result(0xC90113FA);
                break;
            }
            std::vector<u8> pl(8 + dsize);
            uds_lan::wbe16(&pl[0], dest_node);
            pl[2] = dchan;
            pl[3] = flags;
            uds_lan::wbe16(&pl[4], ++tx_seq);
            uds_lan::wbe16(&pl[6], (u16)dsize);
            if (dsize) std::memcpy(&pl[8], gptr(src), dsize);
            auto pkt = make_udp_packet(kMsgData, my_node_id, pl.data(), (u16)pl.size());

            bool bcast = (dest_node == 0xFFFF || (flags & 2));
            for (const auto &p : peers) {
                if (bcast || p.node_id == dest_node)
                    udp.send_unicast(p.ip_be, p.port_be, pkt.data(), pkt.size());
            }
            ipc.ok();
            break;
        }

        case 0x0014: {  // PullPacket
            poll_udp_locked();
            u32 bnode = ipc[1];
            u32 bsz = 0;
            u32 dst = ipc.static_buffer(0, &bsz);
            auto it = std::find_if(binds.begin(), binds.end(), [&](const BindEntry &b) { return b.bind_node_id == bnode; });
            if (it == binds.end() || it->queue.empty()) {
                if (dst && bsz) std::memset(gptr(dst), 0, bsz);
                ipc.ok(3, 2);
                ipc.set(2, 0);
                ipc.set(3, 0);
                ipc.static_desc(4, 0, bsz);
                break;
            }
            auto pkt = std::move(it->queue.front());
            it->queue.pop_front();
            u32 n = std::min<u32>((u32)pkt.data.size(), bsz);
            if (dst && n) std::memcpy(gptr(dst), pkt.data.data(), n);
            if (!it->queue.empty() && it->event) it->event->signal();
            ipc.ok(3, 2);
            ipc.set(2, n);
            ipc.set(3, pkt.src_node_id);
            ipc.static_desc(4, 0, n);
            break;
        }

        case 0x0005:  // EjectClient
        case 0x0006: {  // EjectSpectator
            u16 target = (u16)ipc[1];
            if (conn_status == (u32)uds_lan::NetworkStatus::ConnectedAsHost) {
                auto pkt = make_udp_packet(kMsgLeave, my_node_id, nullptr, 0);
                for (const auto &p : peers) {
                    if (target == 0xFFFF || p.node_id == target) udp.send_unicast(p.ip_be, p.port_be, pkt.data(), pkt.size());
                }
                for (u32 i = 1; i < uds_lan::kUdsMaxNodes; i++) {
                    if (target == 0xFFFF || nodes_ids[i] == target) {
                        if (nodes_ids[i]) {
                            nodes_ids[i] = 0;
                            node_infos[i].clear();
                            node_bitmask &= ~(u16)(1u << i);
                            if (total_nodes > 1) total_nodes--;
                            changed_nodes |= (u16)(1u << i);
                        }
                    }
                }
                net_info.set_total_nodes(total_nodes);
                peers.erase(std::remove_if(peers.begin(), peers.end(),
                    [&](const PeerEndpoint &p) { return target == 0xFFFF || p.node_id == target; }), peers.end());
                send_node_map();
                broadcast_beacon();
                status->signal();
            }
            ipc.ok();
            break;
        }

        case 0x001A:  // GetChannel
            ipc.ok(2);
            ipc.set(2, (conn_status != (u32)uds_lan::NetworkStatus::NotConnected) ? 11 : 0);
            break;

        case 0x0008:  // DestroyNetwork
        case 0x000A: {  // DisconnectNetwork
            if (udp.ready() && my_node_id) {
                auto pkt = make_udp_packet(kMsgLeave, my_node_id, nullptr, 0);
                for (const auto &p : peers) udp.send_unicast(p.ip_be, p.port_be, pkt.data(), pkt.size());
            }
            reset_network_state((u32)uds_lan::NetworkStatus::NotConnected, (u32)uds_lan::DisconnectReason::NotConnected);
            ipc.ok();
            break;
        }

        case 0x0003:  // Shutdown
            if (udp.ready() && my_node_id) {
                auto pkt = make_udp_packet(kMsgLeave, my_node_id, nullptr, 0);
                for (const auto &p : peers) udp.send_unicast(p.ip_be, p.port_be, pkt.data(), pkt.size());
            }
            reset_network_state((u32)uds_lan::NetworkStatus::NotConnected, (u32)uds_lan::DisconnectReason::NotConnected);
            binds.clear();
            initialized = false;
            g_uds_active.store(false);
            udp.close();
            ipc.ok();
            break;

        case 0x0002:  // Scrap
        case 0x0015:  // SetMaxSendDelay
        case 0x0020:  // Flush
        case 0x0021:  // SetProbeResponseParam
            ipc.ok();
            break;
        default:
            unimplemented(ipc);
        }
    }
};

void uds_tick() {
    if (g_uds_active.load() && g_uds_instance) g_uds_instance->tick_locked();
}

struct Generic : Service {
    using Service::Service;
    void handle(IPC &ipc) override { unimplemented(ipc); }
};

// ssl:C has no network behind it, but games take their random numbers from it: Pokemon Ultra Sun seeds its own generator with them at
// every start and save. Unanswered, the buffer stayed as the game left it, and every start rolled the same.
struct Ssl : Service {
    using Service::Service;
    void handle(IPC &ipc) override {
        if (ipc.cmd() != 0x0011) return unimplemented(ipc);
        // GenerateRandomData(size, [buffer]): the clock and the tick it's first asked at, stirred (splitmix64)
        static u64 state = (u64)time(nullptr) << 32 ^ system_tick();
        u64 z = 0;
        for (u32 i = 0; i < std::min(ipc[1], ipc[2] >> 4); i++) {  // (no further than the buffer it names)
            if (i % 8 == 0) {
                z = (state += 0x9E3779B97F4A7C15ull);
                z = (z ^ z >> 30) * 0xBF58476D1CE4E5B9ull;
                z = (z ^ z >> 27) * 0x94D049BB133111EBull;
                z ^= z >> 31;
            }
            w8(ipc[3] + i, (u8)(z >> (i % 8 * 8)));
        }
        ipc.ok(1, 2);  // (the buffer's two words go back as they came)
    }
};

// dlp:SRVR: Download Play's server side. A game asks it whether it was itself started as a Download Play child; the answer word left out
// (as an unimplemented command returns) read as "yes" from what the last request left there: Luigi's Mansion: Dark Moon then started in
// its Thrill Tower multiplayer, as a child looking for its host ("Connection error", "Waiting for player"), not at its title screen.
struct DlpSrvr : Service {
    using Service::Service;
    void handle(IPC &ipc) override {
        switch (ipc.cmd()) {
        case 0x000E:  // IsChild(process id)
            ipc.ok(2);
            ipc.set(2, 0);
            break;
        default:
            unimplemented(ipc);
        }
    }
};

// am:app: the add-on content and update the builder put beside the game (builder/dumps.py), as Azahar's AM hands them out: dlc_contents.bin
// holds the DLC title's content infos (index, type, id, size, ownership; 0x18 bytes each), dlc_title.bin and update_title.bin a title info
// each (title id, size, version, type), dlc_ticket.bin a ticket info. With none of them nothing is installed.
struct Am : Service {
    using Service::Service;
    static std::vector<u8> records(const char *name) {
        std::vector<u8> d;
        if (FILE *f = fopen((g_game.dir + "/" + name).c_str(), "rb")) {
            u8 buf[4096];
            for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) d.insert(d.end(), buf, buf + n);
            fclose(f);
        }
        d.resize(d.size() / 0x18 * 0x18);
        return d;
    }
    static u64 tid_of(const std::vector<u8> &r, size_t i = 0) {
        u64 v;
        memcpy(&v, &r[i * 0x18], 8);
        return le64(v);
    }
    // the DLC title's records, if the request is about it
    static std::vector<u8> dlc_contents(u64 tid) {
        auto t = records("dlc_title.bin");
        return !t.empty() && tid_of(t) == tid ? records("dlc_contents.bin") : std::vector<u8>{};
    }
    static u32 put(const std::vector<u8> &r, u32 start, u32 count, u32 dst) {
        u32 n = 0;
        for (size_t i = start; i < r.size() / 0x18 && n < count; i++, n++) memcpy(gptr(dst + n * 0x18), &r[i * 0x18], 0x18);
        return n;
    }
    // Nothing installed answers "not found", and a count of none: "success" with nothing written would leave the request's own words where the
    // answer goes. New Super Mario Bros. 2's Coin Rush read its DLC title id's high word (0x0004008C) back as the number of DLC tickets it owns (2026-11-07).
    void handle(IPC &ipc) override {
        const u32 not_found = 0xD8A08078;  // AM, invalid state, not found
        switch (ipc.cmd()) {
        case 0x1001: {  // GetDLCContentInfoCount(media type, title id)
            auto r = dlc_contents((u64)ipc[2] | (u64)ipc[3] << 32);
            if (r.empty()) {
                ipc.result(not_found);
                break;
            }
            ipc.ok(2);
            ipc.set(2, (u32)(r.size() / 0x18));
            break;
        }
        case 0x1002: {  // FindDLCContentInfos(media type, title id, count, [content indices], [content infos])
            auto r = dlc_contents((u64)ipc[2] | (u64)ipc[3] << 32);
            if (r.empty()) {
                ipc.result(not_found);
                break;
            }
            const u32 count = ipc[4], ids = ipc[6], dst = ipc[8];
            for (u32 i = 0; i < count; i++)
                for (size_t k = 0; k < r.size() / 0x18; k++)
                    if ((r[k * 0x18] | r[k * 0x18 + 1] << 8) == r16(ids + 2 * i)) memcpy(gptr(dst + i * 0x18), &r[k * 0x18], 0x18);
            ipc.ok(1, 4);
            ipc.set(2, ipc[5]);
            ipc.set(3, ids);
            ipc.set(4, ipc[7]);
            ipc.set(5, dst);
            break;
        }
        case 0x1003: {  // ListDLCContentInfos(count, media type, title id, start, [content infos]) -> how many
            auto r = dlc_contents((u64)ipc[3] | (u64)ipc[4] << 32);
            if (r.empty()) {
                ipc.result(not_found);
                break;
            }
            const u32 n = put(r, ipc[5], ipc[1], ipc[7]);
            ipc.ok(2, 2);
            ipc.set(2, n);
            ipc.set(3, ipc[6]);
            ipc.set(4, ipc[7]);
            break;
        }
        case 0x1005:    // GetDLCTitleInfos(media type, count, [title ids], [title infos])
        case 0x100D: {  // GetPatchTitleInfos(media type, count, [title ids], [title infos])
            auto r = records(ipc.cmd() == 0x1005 ? "dlc_title.bin" : "update_title.bin");
            const u32 count = ipc[2], ids = ipc[4], dst = ipc[6];
            u32 res = r.empty() ? not_found : 0;
            for (u32 i = 0; i < count && !res; i++) {
                const u64 tid = (u64)r32(ids + 8 * i) | (u64)r32(ids + 8 * i + 4) << 32;
                if (tid_of(r) == tid) memcpy(gptr(dst + i * 0x18), r.data(), 0x18);
                else res = not_found;
            }
            if (res) {
                ipc.result(res);
                break;
            }
            ipc.ok(1, 4);
            ipc.set(2, ipc[3]);
            ipc.set(3, ids);
            ipc.set(4, ipc[5]);
            ipc.set(5, dst);
            break;
        }
        case 0x1006: {  // GetNumDataTitleTickets(title id) -> count
            auto r = records("dlc_ticket.bin");
            ipc.ok(2);
            ipc.set(2, !r.empty() && tid_of(r) == ((u64)ipc[1] | (u64)ipc[2] << 32) ? (u32)(r.size() / 0x18) : 0);
            break;
        }
        case 0x1007:    // ListDLCTicketInfos(count, title id, start, [ticket infos]) -> how many
        case 0x1008: {  // ListDataTitleTicketInfos, the same shape
            auto r = records("dlc_ticket.bin");
            const u64 tid = (u64)ipc[2] | (u64)ipc[3] << 32;
            const u32 n = !r.empty() && tid_of(r) == tid ? put(r, ipc[4], ipc[1], ipc[6]) : 0;
            ipc.ok(2, 2);
            ipc.set(2, n);
            ipc.set(3, ipc[5]);
            ipc.set(4, ipc[6]);
            break;
        }
        default:
            unimplemented(ipc);
        }
    }
};

void register_misc() {
    register_service(std::make_shared<Ptm>("ptm:u"));
    register_service(std::make_shared<Ptm>("ptm:sysm"));
#ifndef MK7_PRETENDO_ONLINE
    register_service(std::make_shared<Ac>("ac:u"));
    register_service(std::make_shared<Ac>("ac:i"));
    register_service(std::make_shared<Frd>("frd:u"));
    register_service(std::make_shared<Frd>("frd:a"));
#endif
    register_service(std::make_shared<Ndm>("ndm:u"));
    register_service(std::make_shared<Cecd>("cecd:u"));
    register_service(std::make_shared<Boss>("boss:U"));
    register_service(std::make_shared<Mic>("mic:u"));
    register_service(std::make_shared<Ir>("ir:USER"));
    register_service(std::make_shared<Ir>("ir:u"));
    register_service(std::make_shared<Uds>("nwm::UDS"));
    register_service(std::make_shared<Am>("am:app"));  // was Generic: NSMB2's Coin Rush read its own request back as thousands of DLC tickets (2026-11-07)
    register_service(std::make_shared<DlpSrvr>("dlp:SRVR"));
    register_y2r();
    register_cam();
    register_nfc();
    register_mk7_online();
    register_service(std::make_shared<Ssl>("ssl:C"));
    for (const char *n : {"news:u", "dlp:FKCL",
                          "pxi:dev", "nim:aoc", "qtm:u", "csnd:SND"})
        register_service(std::make_shared<Generic>(n));
}

void register_all_services() {
    register_apt();
    register_fs();
    register_cfg();
    register_gsp();
    register_hid();
    register_dsp();
    register_misc();
    register_ro();
}

// ---- shared system font (APT GetSharedFont): the BCFNT from system archive 0004009B00014002, LZ11-decompressed
// after a 0x80-byte header (status 2 = loaded, region, size), with its section pointers relocated to the
// address where the game sees the block. The block is in the linear heap: the GPU samples the glyph sheets.
static std::vector<u8> lz11(const std::vector<u8> &d) {
    if (d.size() < 4 || d[0] != 0x11) return {};
    size_t size = d[1] | d[2] << 8 | d[3] << 16, p = 4;
    if (!size && d.size() >= 8) size = d[4] | d[5] << 8 | d[6] << 16 | (size_t)d[7] << 24, p = 8;
    std::vector<u8> out;
    out.reserve(size);
    while (out.size() < size && p < d.size()) {
        u8 flags = d[p++];
        for (int b = 0; b < 8 && out.size() < size && p < d.size(); b++) {
            if (!(flags & (0x80 >> b))) {
                out.push_back(d[p++]);
                continue;
            }
            size_t len, disp;
            u8 x = d[p];
            if (x >> 4 == 0) {
                len = ((x & 15) << 4 | d[p + 1] >> 4) + 0x11;
                disp = ((d[p + 1] & 15) << 8 | d[p + 2]) + 1;
                p += 3;
            } else if (x >> 4 == 1) {
                len = ((x & 15) << 12 | d[p + 1] << 4 | d[p + 2] >> 4) + 0x111;
                disp = ((d[p + 2] & 15) << 8 | d[p + 3]) + 1;
                p += 4;
            } else {
                len = (x >> 4) + 1;
                disp = ((x & 15) << 8 | d[p + 1]) + 1;
                p += 2;
            }
            if (disp > out.size()) return {};
            for (size_t i = 0; i < len && out.size() < size; i++) out.push_back(out[out.size() - disp]);
        }
    }
    return out;
}

// Rebases the section pointers of the font at addr + 0x80 so that they point inside the block at addr, and marks
// it resolved ("CFNU", as the system's copy is: the font library would otherwise add its address again).
static void relocate_shared_font(u32 addr) {
    u32 cfnt = addr + 0x80;
    if (memcmp(gptr(cfnt), "CFNT", 4) && memcmp(gptr(cfnt), "CFNU", 4)) return;
    memcpy(gptr(cfnt), "CFNU", 4);
    u32 nblocks = r32(cfnt + 0x10), first = cfnt + r16(cfnt + 6);
    u32 cmap = 0, cmap_ptr = 0, at = first;
    for (u32 i = 0; i < nblocks; i++, at += r32(at + 4)) {
        if (!cmap && !memcmp(gptr(at), "CMAP", 4)) cmap = at;
        if (!memcmp(gptr(at), "FINF", 4)) cmap_ptr = r32(at + 24);
    }
    if (!cmap || !cmap_ptr) return;
    u32 delta = (cmap + 8) - cmap_ptr;  // where the pointers assume the font is vs where it is
    if (!delta) return;
    at = first;
    for (u32 i = 0; i < nblocks; i++, at += r32(at + 4)) {
        auto fix = [&](u32 a) { if (r32(a)) w32(a, r32(a) + delta); };
        if (!memcmp(gptr(at), "FINF", 4)) { fix(at + 16); fix(at + 20); fix(at + 24); }
        else if (!memcmp(gptr(at), "TGLP", 4)) fix(at + 28);
        else if (!memcmp(gptr(at), "CWDH", 4)) fix(at + 12);
        else if (!memcmp(gptr(at), "CMAP", 4)) fix(at + 16);
    }
}

std::shared_ptr<SharedMem> load_shared_font() {
    // A build may bring the font already unpacked (shared_font.bcfnt beside the game's files, tools/make_game_title.py): the console then reads it
    // as it is, with no archive to search and nothing to decompress while the game starts. Without the file it comes from the system archive.
    std::vector<u8> font;
    if (FILE *f = fopen((g_game.dir + "/shared_font.bcfnt").c_str(), "rb")) {
        u8 buf[65536];
        for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) font.insert(font.end(), buf, buf + n);
        fclose(f);
    }
    if (font.empty()) font = lz11(system_archive_file(0x0004009B00014002ull, "/cbf_std.bcfnt.lz"));
    const u32 size = 0x332000;
    if (font.empty() || font.size() > size - 0x80) {
        LOG("[apt] shared font: system archive 0004009b00014002 missing or unreadable (%zu bytes)\n", font.size());
        return nullptr;
    }
    auto sm = std::make_shared<SharedMem>();
    sm->name = "shared font";
    sm->size = size;
    sm->backing = alloc_linear(size);
    if (!sm->backing) {
        LOG("[apt] shared font: no linear memory left\n");
        return nullptr;
    }
    memset(gptr(sm->backing), 0, size);
    memcpy(gptr(sm->backing + 0x80), font.data(), font.size());
    w32(sm->backing, 2);  // loaded
    w32(sm->backing + 4, 1);
    w32(sm->backing + 8, (u32)font.size());
    relocate_shared_font(sm->backing);
    sm->on_move = relocate_shared_font;
    LOG("[apt] shared font: %zu bytes at %08x\n", font.size(), sm->backing);
    return sm;
}

}  // namespace kernel
