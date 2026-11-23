// Mario Kart 7 online (Pretendo 3DS) + Miiverse (Pretendo Juxtaposition) service bridge.
// MK7 NASC/NEX/FRD online is active only when compiled with -DMK7_PRETENDO_ONLINE=1.
// Miiverse (olv.nintendo.net -> olv.pretendo.cc, act:u PNID, and Miiverse applet -> Juxtaposition) is active for all games.
// Does NOT modify Wii U's Inkay configuration.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../service.h"
#include "services.h"

#ifndef MK7_PRINCIPAL_ID
#define MK7_PRINCIPAL_ID 0x40000001u
#endif
#ifndef MK7_FRIEND_CODE
#define MK7_FRIEND_CODE 0x0000000040000001ull
#endif

#ifdef __WIIU__
#include <coreinit/dynload.h>
#include <nn/ac/ac_c.h>
#include <nn/act/client_cpp.h>
#include <nsysnet/_netdb.h>
#include <nsysnet/_socket.h>
#include <nsysnet/nssl.h>
#include <sysapp/switch.h>
#endif

namespace kernel {

namespace {

// ---- Helpers
template <typename Fn>
static void run_io(Lock *lk, Fn &&fn) {
    if (lk) run_blocked(*lk, std::forward<Fn>(fn));
    else fn();
}
inline void le16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
inline void le32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
inline u16 rd_le16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
inline u32 rd_le32(const u8 *p) { return (u32)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }

// Compute 3DS Friend Code (local_friend_code) from a 32-bit Principal ID via SHA-1
static u64 fc_from_principal_id(u32 pid) {
    u8 msg[64]{};
    msg[0] = (u8)(pid & 0xFF);
    msg[1] = (u8)((pid >> 8) & 0xFF);
    msg[2] = (u8)((pid >> 16) & 0xFF);
    msg[3] = (u8)((pid >> 24) & 0xFF);
    msg[4] = 0x80;
    msg[63] = 32;  // 4 bytes = 32 bits
    u32 h0 = 0x67452301u, h1 = 0xEFCDAB89u, h2 = 0x98BADCFEu, h3 = 0x10325476u, h4 = 0xC3D2E1F0u;
    u32 w[80]{};
    for (int i = 0; i < 16; i++) {
        w[i] = ((u32)msg[i * 4] << 24) | ((u32)msg[i * 4 + 1] << 16) | ((u32)msg[i * 4 + 2] << 8) | msg[i * 4 + 3];
    }
    auto rol = [](u32 v, int s) { return (v << s) | (v >> (32 - s)); };
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    u32 a = h0, b = h1, c = h2, d = h3, e = h4;
    for (int i = 0; i < 80; i++) {
        u32 f = (i < 20) ? ((b & c) | ((~b) & d)) : (i < 40) ? (b ^ c ^ d) : (i < 60) ? ((b & c) | (b & d) | (c & d)) : (b ^ c ^ d);
        u32 k = (i < 20) ? 0x5A827999u : (i < 40) ? 0x6ED9EBA1u : (i < 60) ? 0x8F1BBCDCu : 0xCA62C1D6u;
        u32 tmp = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = tmp;
    }
    h0 += a;
    u8 first_byte = (u8)((h0 >> 24) & 0xFF);
    return ((u64)(first_byte >> 1) << 32) | (u64)pid;
}

struct PnIdIdentity {
    bool loaded = false;
    bool from_wiiu = false;
    u32 pid = MK7_PRINCIPAL_ID;
    u64 fc = MK7_FRIEND_CODE;
    char account_id[17]{};
    u16 mii_name[11]{};
    std::array<u8, 0x60> mii{};
};
PnIdIdentity g_pnid;

#ifdef __WIIU__
// Dynamic loaders for nn_ac.rpl and nsysnet.rpl (loaded only when MK7 uses online)
struct AcDyn {
    bool tried = false, ok = false;
    decltype(&ACInitialize) init = nullptr;
    decltype(&ACConnect) connect = nullptr;
    decltype(&ACIsApplicationConnected) is_connected = nullptr;
    decltype(&ACGetAssignedAddress) get_ip = nullptr;

    bool ensure() {
        if (tried) return ok;
        tried = true;
        OSDynLoad_Module m{};
        if (OSDynLoad_Acquire("nn_ac.rpl", &m) != OS_DYNLOAD_OK) return false;
        if (OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "ACInitialize", (void **)&init) != OS_DYNLOAD_OK) return false;
        if (OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "ACConnect", (void **)&connect) != OS_DYNLOAD_OK) return false;
        if (OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "ACIsApplicationConnected", (void **)&is_connected) != OS_DYNLOAD_OK) return false;
        if (OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "ACGetAssignedAddress", (void **)&get_ip) != OS_DYNLOAD_OK) return false;
        init();
        ok = true;
        return true;
    }
} g_ac;

// Dynamic loader for nn_act.rpl: reads the active Wii U Pretendo (PNID) account
static void load_wiiu_pnid() {
    if (g_pnid.loaded) return;
    g_pnid.loaded = true;
    g_pnid.mii = player_mii();

    OSDynLoad_Module m{};
    if (OSDynLoad_Acquire("nn_act.rpl", &m) != OS_DYNLOAD_OK) {
        LOG("[mk7-act] nn_act.rpl unavailable; using build FC\n");
        return;
    }
    decltype(&nn::act::Initialize) pInit = nullptr;
    decltype(&nn::act::GetSlotNo) pGetSlot = nullptr;
    decltype(&nn::act::IsNetworkAccountEx) pIsNet = nullptr;
    decltype(&nn::act::GetPrincipalIdEx) pGetPid = nullptr;
    decltype(&nn::act::GetAccountIdEx) pGetAcc = nullptr;
    decltype(&nn::act::GetMiiNameEx) pGetName = nullptr;
    decltype(&nn::act::GetMiiEx) pGetMii = nullptr;

    if (OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "Initialize__Q2_2nn3actFv", (void **)&pInit) != OS_DYNLOAD_OK ||
        OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "GetSlotNo__Q2_2nn3actFv", (void **)&pGetSlot) != OS_DYNLOAD_OK ||
        OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "IsNetworkAccountEx__Q2_2nn3actFUc", (void **)&pIsNet) != OS_DYNLOAD_OK ||
        OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "GetPrincipalIdEx__Q2_2nn3actFPUiUc", (void **)&pGetPid) != OS_DYNLOAD_OK ||
        OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "GetAccountIdEx__Q2_2nn3actFPcUc", (void **)&pGetAcc) != OS_DYNLOAD_OK) {
        LOG("[mk7-act] missing nn_act symbols; using build FC\n");
        return;
    }
    OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "GetMiiNameEx__Q2_2nn3actFPwUc", (void **)&pGetName);
    OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "GetMiiEx__Q2_2nn3actFP12FFLStoreDataUc", (void **)&pGetMii);

    pInit();
    nn::act::SlotNo slot = pGetSlot();
    nn::act::PrincipalId pid = 0;
    if (pIsNet(slot) && pGetPid(&pid, slot).IsSuccess() && pid != 0) {
        g_pnid.pid = pid;
        g_pnid.fc = fc_from_principal_id(pid);
        g_pnid.from_wiiu = true;
        pGetAcc(g_pnid.account_id, slot);
        if (pGetName) {
            int16_t raw_name[11]{};
            if (pGetName(raw_name, slot).IsSuccess()) {
                for (int i = 0; i < 11; i++) g_pnid.mii_name[i] = (u16)raw_name[i];
            }
        }
        if (pGetMii) {
            FFLStoreData ffl{};
            if (pGetMii(&ffl, slot).IsSuccess()) {
                std::array<u8, 0x60> host{}, guest{};
                memcpy(host.data(), &ffl, 0x60);
                if (mii_3ds(host, guest)) g_pnid.mii = guest;
            }
        }
        LOG("[mk7-act] Wii U PNID='%s' PID=%08x FC=%012llu\n", g_pnid.account_id,
            (unsigned)g_pnid.pid, (unsigned long long)g_pnid.fc);
    } else {
        LOG("[mk7-act] no linked network account on slot %u; using build PID=%08x\n",
            (unsigned)slot, (unsigned)g_pnid.pid);
    }
}

struct NetDyn {
    bool tried = false, ok = false, ssl_ok = false;
    int (*p_socket_lib_init)() = nullptr;
    int (*p_socket)(int, int, int) = nullptr;
    int (*p_socketclose)(int) = nullptr;
    int (*p_connect)(int, const struct sockaddr *, int) = nullptr;
    int (*p_bind)(int, const struct sockaddr *, int) = nullptr;
    int (*p_listen)(int, int) = nullptr;
    int (*p_accept)(int, struct sockaddr *, int *) = nullptr;
    int (*p_send)(int, const void *, int, int) = nullptr;
    int (*p_sendto)(int, const void *, int, int, const struct sockaddr *, int) = nullptr;
    int (*p_recv)(int, void *, int, int) = nullptr;
    int (*p_recvfrom)(int, void *, int, int, struct sockaddr *, int *) = nullptr;
    int (*p_select)(int, nsysnet_fd_set *, nsysnet_fd_set *, nsysnet_fd_set *, nsysnet_timeval *) = nullptr;
    int (*p_getsockopt)(int, int, int, void *, int *) = nullptr;
    int (*p_setsockopt)(int, int, int, const void *, int) = nullptr;
    int (*p_getsockname)(int, struct sockaddr *, int *) = nullptr;
    int (*p_getpeername)(int, struct sockaddr *, int *) = nullptr;
    int (*p_shutdown)(int, int) = nullptr;
    int (*p_socketlasterr)() = nullptr;
    struct hostent *(*p_gethostbyname)(const char *) = nullptr;
    int (*p_getaddrinfo)(const char *, const char *, const struct addrinfo *, struct addrinfo **) = nullptr;
    void (*p_freeaddrinfo)(struct addrinfo *) = nullptr;

    decltype(&NSSLInit) p_nssl_init = nullptr;
    decltype(&NSSLCreateContext) p_nssl_ctx = nullptr;
    decltype(&NSSLDestroyContext) p_nssl_ctx_free = nullptr;
    decltype(&NSSLAddServerPKI) p_nssl_add_pki = nullptr;
    decltype(&NSSLCreateConnection) p_nssl_conn = nullptr;
    decltype(&NSSLDestroyConnection) p_nssl_conn_free = nullptr;
    decltype(&NSSLRead) p_nssl_read = nullptr;
    decltype(&NSSLWrite) p_nssl_write = nullptr;

    bool ensure() {
        if (tried) return ok;
        tried = true;
        OSDynLoad_Module m{};
        if (OSDynLoad_Acquire("nsysnet.rpl", &m) != OS_DYNLOAD_OK) {
            LOG("[mk7-net] nsysnet.rpl didn't load\n");
            return false;
        }
        struct Sym { const char *n; void **p; };
        const Sym syms[] = {
            {"socket_lib_init", (void **)&p_socket_lib_init},
            {"socket", (void **)&p_socket},
            {"socketclose", (void **)&p_socketclose},
            {"connect", (void **)&p_connect},
            {"bind", (void **)&p_bind},
            {"listen", (void **)&p_listen},
            {"accept", (void **)&p_accept},
            {"send", (void **)&p_send},
            {"sendto", (void **)&p_sendto},
            {"recv", (void **)&p_recv},
            {"recvfrom", (void **)&p_recvfrom},
            {"select", (void **)&p_select},
            {"getsockopt", (void **)&p_getsockopt},
            {"setsockopt", (void **)&p_setsockopt},
            {"getsockname", (void **)&p_getsockname},
            {"getpeername", (void **)&p_getpeername},
            {"shutdown", (void **)&p_shutdown},
            {"socketlasterr", (void **)&p_socketlasterr},
            {"gethostbyname", (void **)&p_gethostbyname},
            {"getaddrinfo", (void **)&p_getaddrinfo},
            {"freeaddrinfo", (void **)&p_freeaddrinfo},
        };
        for (const auto &s : syms) {
            if (OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, s.n, s.p) != OS_DYNLOAD_OK || !*s.p) {
                LOG("[mk7-net] missing nsysnet symbol: %s\n", s.n);
                return false;
            }
        }
        p_socket_lib_init();
        ok = true;

        if (OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "NSSLInit", (void **)&p_nssl_init) == OS_DYNLOAD_OK &&
            OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "NSSLCreateContext", (void **)&p_nssl_ctx) == OS_DYNLOAD_OK &&
            OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "NSSLDestroyContext", (void **)&p_nssl_ctx_free) == OS_DYNLOAD_OK &&
            OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "NSSLAddServerPKI", (void **)&p_nssl_add_pki) == OS_DYNLOAD_OK &&
            OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "NSSLCreateConnection", (void **)&p_nssl_conn) == OS_DYNLOAD_OK &&
            OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "NSSLDestroyConnection", (void **)&p_nssl_conn_free) == OS_DYNLOAD_OK &&
            OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "NSSLRead", (void **)&p_nssl_read) == OS_DYNLOAD_OK &&
            OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "NSSLWrite", (void **)&p_nssl_write) == OS_DYNLOAD_OK) {
            p_nssl_init();
            ssl_ok = true;
        }
        return true;
    }
} g_net;

static void ctr_to_wiiu_sockaddr(const u8 *src, u32 len, struct sockaddr_in *dst) {
    memset(dst, 0, sizeof(*dst));
    dst->sin_family = AF_INET;
    if (len >= 8) {
        dst->sin_port = (u16)((src[2] << 8) | src[3]);
        memcpy(&dst->sin_addr.s_addr, src + 4, 4);
    }
}

static void wiiu_to_ctr_sockaddr(const struct sockaddr_in *src, u8 *dst, u32 len) {
    memset(dst, 0, len);
    if (len >= 8) {
        dst[0] = 8;
        dst[1] = 2;  // AF_INET
        dst[2] = (u8)(src->sin_port >> 8);
        dst[3] = (u8)(src->sin_port & 0xFF);
        memcpy(dst + 4, &src->sin_addr.s_addr, 4);
    }
}
#endif

// ---- ac:u / ac:i
struct AcMk7 : Service {
    using Service::Service;
    bool connected = false;
    void handle(IPC &ipc) override {
        switch (ipc.cmd()) {
        case 0x0001: {  // CreateDefaultConfig
            u32 dst = ipc.static_buffer(0);
            memset(gptr(dst), 0, 0x200);
            ipc.ok(1, 2);
            ipc.static_desc(2, 0, 0x200);
            break;
        }
        case 0x0004: {  // ConnectAsync
#ifdef __WIIU__
            bool ok_conn = false;
            run_io(ipc.lk, [&] {
                if (g_ac.ensure()) {
                    g_ac.connect();
                    BOOL ok = FALSE;
                    g_ac.is_connected(&ok);
                    ok_conn = ok != FALSE;
                }
            });
            connected = ok_conn;
#else
            connected = true;
#endif
            ipc.ok();
            if (auto ev = get<Event>(ipc[5])) ev->signal();
            break;
        }
        case 0x0005:  // GetConnectResult
            ipc.ok();
            break;
        case 0x0008:  // CloseAsync
            connected = false;
            ipc.ok();
            if (auto ev = get<Event>(ipc[3])) ev->signal();
            break;
        case 0x000D:  // GetStatus
        case 0x000F:  // GetWifiStatus
            ipc.ok(2);
            ipc.set(2, 3);  // Connected
            break;
        case 0x003E:  // IsConnected
            ipc.ok(2);
            ipc.set(2, 1);
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

// ---- frd:u / frd:a
struct FrdMk7 : Service {
    using Service::Service;
    bool logged_in = false;
    std::shared_ptr<Event> notif_event = make_event(ONESHOT, "FRD::notif");

    void handle(IPC &ipc) override {
        switch (ipc.cmd()) {
        case 0x0001:  // HasLoggedIn
        case 0x0002:  // IsOnline
            ipc.ok(2);
            ipc.set(2, logged_in ? 1u : 1u);
            break;
        case 0x0003: {  // Login(event)
#ifdef __WIIU__
            run_io(ipc.lk, [&] { load_wiiu_pnid(); });
#endif
            logged_in = true;
            LOG("[mk7-frd] Login with PID=%08x FC=%016llx (wiiu_pnid=%d)\n", (unsigned)g_pnid.pid,
                (unsigned long long)g_pnid.fc, (int)g_pnid.from_wiiu);
            ipc.ok();
            if (auto ev = get<Event>(ipc[2])) ev->signal();
            break;
        }
        case 0x0004:  // Logout
            logged_in = false;
            ipc.ok();
            break;
        case 0x0005:  // GetMyFriendKey
#ifdef __WIIU__
            load_wiiu_pnid();
#endif
            ipc.ok(5);
            ipc.set(2, g_pnid.pid);
            ipc.set(3, 0);
            ipc.set64(4, g_pnid.fc);
            break;
        case 0x0006:  // GetMyPreference
            ipc.ok(4);
            ipc.set(2, 1);
            ipc.set(3, 1);
            ipc.set(4, 1);
            break;
        case 0x0007:  // GetMyProfile
            ipc.ok(3);
            ipc.set(2, (1u) | (49u << 8));  // USA
            ipc.set(3, 1u);                 // Platform 3DS
            break;
        case 0x0008: {  // GetMyPresence
            u32 dst = ipc.static_buffer(0);
            memset(gptr(dst), 0, 0x12C);
            ipc.ok(1, 2);
            ipc.static_desc(2, 0, 0x12C);
            break;
        }
        case 0x0009: {  // GetMyScreenName
#ifdef __WIIU__
            load_wiiu_pnid();
#endif
            const auto &mii = g_pnid.from_wiiu ? g_pnid.mii : player_mii();
            ipc.ok(7);
            memset(gptr(ipc.buf + 8), 0, 24);
            for (u32 i = 0; i < 10; i++) w16(ipc.buf + 8 + 2 * i, (u16)(mii[0x1A + 2 * i] | mii[0x1B + 2 * i] << 8));
            break;
        }
        case 0x000A: {  // GetMyMii
#ifdef __WIIU__
            load_wiiu_pnid();
#endif
            const auto &mii = g_pnid.from_wiiu ? g_pnid.mii : player_mii();
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
        case 0x001E:  // UpdateGameModeDescription
        case 0x0021:  // SetNotificationMask
        case 0x0032:  // SetClientSdkVersion
            ipc.ok();
            break;
        case 0x0020:  // AttachToEventNotification
            ipc.ok(1, 2);
            ipc.handles(2, {notif_event});
            break;
        default:
            unimplemented(ipc);
        }
    }
};

// ---- soc:U
struct SocMk7 : Service {
    using Service::Service;
    void handle(IPC &ipc) override {
#ifndef __WIIU__
        unimplemented(ipc);
#else
        if (!g_net.tried) {
            run_io(ipc.lk, [&] { g_net.ensure(); });
        }
        if (!g_net.ensure()) {
            ipc.result(0xD8E007F7);
            return;
        }
        switch (ipc.cmd()) {
        case 0x0001:  // InitializeSockets
        case 0x0019:  // ShutdownSockets
            ipc.ok(2);
            ipc.set(2, 0);
            break;
        case 0x0002: {  // Socket(domain, type, protocol)
            int fd = g_net.p_socket((int)ipc[1], (int)ipc[2], (int)ipc[3]);
            ipc.ok(2);
            ipc.set(2, (u32)(fd < 0 ? -g_net.p_socketlasterr() : fd));
            break;
        }
        case 0x0003: {  // Listen(fd, backlog)
            int r = g_net.p_listen((int)ipc[1], (int)ipc[2]);
            ipc.ok(2);
            ipc.set(2, (u32)(r < 0 ? -g_net.p_socketlasterr() : 0));
            break;
        }
        case 0x0004: {  // Accept(fd, addr_len, pid) -> addr buffer
            struct sockaddr_in sa{};
            int slen = sizeof(sa);
            int fd = (int)ipc[1];
            int r = -1;
            run_io(ipc.lk, [&] { r = g_net.p_accept(fd, (struct sockaddr *)&sa, &slen); });
            u32 dst = ipc.static_buffer(0);
            if (r >= 0 && dst) wiiu_to_ctr_sockaddr(&sa, gptr(dst), ipc[2]);
            ipc.ok(2, 2);
            ipc.set(2, (u32)(r < 0 ? -g_net.p_socketlasterr() : r));
            ipc.static_desc(3, 0, r >= 0 ? ipc[2] : 0);
            break;
        }
        case 0x0005: {  // Bind(fd, len, pid, static addr)
            struct sockaddr_in sa{};
            ctr_to_wiiu_sockaddr(gptr(ipc[5]), ipc[2], &sa);
            int r = g_net.p_bind((int)ipc[1], (const struct sockaddr *)&sa, sizeof(sa));
            ipc.ok(2);
            ipc.set(2, (u32)(r < 0 ? -g_net.p_socketlasterr() : 0));
            break;
        }
        case 0x0006: {  // Connect(fd, len, pid, static addr)
            struct sockaddr_in sa{};
            ctr_to_wiiu_sockaddr(gptr(ipc[5]), ipc[2], &sa);
            int fd = (int)ipc[1];
            int r = -1;
            run_io(ipc.lk, [&] { r = g_net.p_connect(fd, (const struct sockaddr *)&sa, sizeof(sa)); });
            ipc.ok(2);
            ipc.set(2, (u32)(r < 0 ? -g_net.p_socketlasterr() : 0));
            break;
        }
        case 0x0007:    // RecvFromOther
        case 0x0008: {  // RecvFrom(fd, len, flags, addr_len, pid, buf)
            struct sockaddr_in sa{};
            int slen = sizeof(sa);
            const int fd = (int)ipc[1];
            const u32 len = ipc[2], flags = ipc[3], alen = ipc[4];
            const u32 buf = ipc[8];
            void *bptr = gptr(buf);
            int r = -1;
            run_io(ipc.lk, [&] {
                r = g_net.p_recvfrom(fd, bptr, (int)len, (int)flags,
                                     alen ? (struct sockaddr *)&sa : nullptr, alen ? &slen : nullptr);
            });
            u32 addr_dst = ipc.static_buffer(0);
            if (r >= 0 && alen && addr_dst) wiiu_to_ctr_sockaddr(&sa, gptr(addr_dst), alen);
            ipc.ok(2, 2);
            ipc.set(2, (u32)(r < 0 ? -g_net.p_socketlasterr() : r));
            ipc.static_desc(3, 0, (r >= 0 && alen) ? alen : 0);
            break;
        }
        case 0x0009:    // SendToOther
        case 0x000A: {  // SendTo(fd, len, flags, addr_len, pid, buf, addr)
            struct sockaddr_in sa{};
            const int fd = (int)ipc[1];
            const u32 len = ipc[2], flags = ipc[3], alen = ipc[4];
            const u32 buf = ipc[8];
            const u32 addr_ptr = ipc[10];
            const void *bptr = gptr(buf);
            bool has_addr = (alen && addr_ptr);
            if (has_addr) ctr_to_wiiu_sockaddr(gptr(addr_ptr), alen, &sa);
            int r = -1;
            run_io(ipc.lk, [&] {
                if (has_addr) r = g_net.p_sendto(fd, bptr, (int)len, (int)flags, (const struct sockaddr *)&sa, sizeof(sa));
                else r = g_net.p_send(fd, bptr, (int)len, (int)flags);
            });
            ipc.ok(2);
            ipc.set(2, (u32)(r < 0 ? -g_net.p_socketlasterr() : r));
            break;
        }
        case 0x000B: {  // Close(fd)
            int r = g_net.p_socketclose((int)ipc[1]);
            ipc.ok(2);
            ipc.set(2, (u32)(r < 0 ? -g_net.p_socketlasterr() : 0));
            break;
        }
        case 0x000C: {  // Shutdown(fd, how)
            int r = g_net.p_shutdown((int)ipc[1], (int)ipc[2]);
            ipc.ok(2);
            ipc.set(2, (u32)(r < 0 ? -g_net.p_socketlasterr() : 0));
            break;
        }
        case 0x000D: {  // GetHostByName(name_len, out_len, name_buf, out_buf)
            std::string name((const char *)gptr(ipc[4]));
            auto rep_host = [&](const char *from, const char *to) {
                size_t pos = name.find(from);
                if (pos != std::string::npos) name.replace(pos, strlen(from), to);
            };
            rep_host("nintendowifi.net", "pretendo.cc");
            rep_host("nintendo.net", "pretendo.cc");
            u32 out = ipc[6], out_len = ipc[2];
            struct hostent *he = nullptr;
            run_io(ipc.lk, [&] { he = g_net.p_gethostbyname(name.c_str()); });
            memset(gptr(out), 0, out_len);
            if (!he || !he->h_addr_list || !he->h_addr_list[0] || out_len < 32) {
                ipc.ok(2);
                ipc.set(2, (u32)-1);
                break;
            }
            u8 *o = gptr(out);
            strncpy((char *)o, he->h_name ? he->h_name : name.c_str(), 15);
            le16(o + 16, 2);  // AF_INET
            le16(o + 18, 4);  // addrlen
            o[20] = 1;        // count
            memcpy(o + 24, he->h_addr_list[0], 4);
            ipc.ok(2);
            ipc.set(2, 0);
            break;
        }
        case 0x0011: {  // GetSockOpt(fd, level, optname, optlen)
            int len = (int)ipc[4];
            u32 dst = ipc.static_buffer(0);
            int r = g_net.p_getsockopt((int)ipc[1], (int)ipc[2], (int)ipc[3], gptr(dst), &len);
            ipc.ok(3, 2);
            ipc.set(2, (u32)(r < 0 ? -g_net.p_socketlasterr() : 0));
            ipc.set(3, (u32)len);
            ipc.static_desc(4, 0, (u32)std::max(0, len));
            break;
        }
        case 0x0012: {  // SetSockOpt(fd, level, optname, optlen, optval)
            int optval = (int)rd_le32(gptr(ipc[6]));
            int r = g_net.p_setsockopt((int)ipc[1], (int)ipc[2], (int)ipc[3], &optval, sizeof(optval));
            ipc.ok(2);
            ipc.set(2, (u32)(r < 0 ? -g_net.p_socketlasterr() : 0));
            break;
        }
        case 0x0013: {  // Fcntl(fd, cmd, arg)
            int fd = (int)ipc[1], cmd = (int)ipc[2], arg = (int)ipc[3];
            if (cmd == 4) {  // F_SETFL -> O_NONBLOCK (0x04 on 3DS)
                int nb = (arg & 0x04) ? 1 : 0;
                g_net.p_setsockopt(fd, SOL_SOCKET, SO_NBIO, &nb, sizeof(nb));
            }
            ipc.ok(2);
            ipc.set(2, 0);
            break;
        }
        case 0x0014: {  // Poll(fds_ptr, nfds, timeout_ms)
            const u32 nfds = std::min<u32>(ipc[1], 64);
            const u32 p = ipc[5];
            nsysnet_fd_set rfds{}, wfds{}, efds{};
            NSYSNET_FD_ZERO(&rfds);
            NSYSNET_FD_ZERO(&wfds);
            NSYSNET_FD_ZERO(&efds);
            int maxfd = -1;
            for (u32 i = 0; i < nfds; i++) {
                int fd = (int)rd_le32(gptr(p + i * 8));
                u16 ev = rd_le16(gptr(p + i * 8 + 4));
                if (fd >= 0 && fd < NSYSNET_FD_SETSIZE) {
                    if (ev & 1) NSYSNET_FD_SET(fd, &rfds);
                    if (ev & 4) NSYSNET_FD_SET(fd, &wfds);
                    NSYSNET_FD_SET(fd, &efds);
                    maxfd = std::max(maxfd, fd);
                }
                le16(gptr(p + i * 8 + 6), 0);
            }
            nsysnet_timeval tv{0, 0};  // non-blocking poll to avoid stalling kernel mutex
            int r = maxfd >= 0 ? g_net.p_select(maxfd + 1, &rfds, &wfds, &efds, &tv) : 0;
            if (r > 0) {
                for (u32 i = 0; i < nfds; i++) {
                    int fd = (int)rd_le32(gptr(p + i * 8));
                    u16 rev = 0;
                    if (fd >= 0 && fd < NSYSNET_FD_SETSIZE) {
                        if (NSYSNET_FD_ISSET(fd, &rfds)) rev |= 1;
                        if (NSYSNET_FD_ISSET(fd, &wfds)) rev |= 4;
                        if (NSYSNET_FD_ISSET(fd, &efds)) rev |= 8;
                    }
                    le16(gptr(p + i * 8 + 6), rev);
                }
            }
            ipc.ok(2);
            ipc.set(2, (u32)(r < 0 ? -g_net.p_socketlasterr() : r));
            break;
        }
        case 0x0016: {  // GetHostId
            uint32_t ip = 0;
            if (g_ac.ensure()) g_ac.get_ip(&ip);
            ipc.ok(2);
            ipc.set(2, ip);
            break;
        }
        case 0x0017:    // GetSockName
        case 0x0018: {  // GetPeerName
            struct sockaddr_in sa{};
            int slen = sizeof(sa);
            int r = ipc.cmd() == 0x0017 ? g_net.p_getsockname((int)ipc[1], (struct sockaddr *)&sa, &slen)
                                        : g_net.p_getpeername((int)ipc[1], (struct sockaddr *)&sa, &slen);
            u32 dst = ipc.static_buffer(0);
            if (r >= 0 && dst) wiiu_to_ctr_sockaddr(&sa, gptr(dst), ipc[2]);
            ipc.ok(2, 2);
            ipc.set(2, (u32)(r < 0 ? -g_net.p_socketlasterr() : 0));
            ipc.static_desc(3, 0, r >= 0 ? ipc[2] : 0);
            break;
        }
        default:
            unimplemented(ipc);
        }
#endif
    }
};

// ---- http:C (Pretendo 3DS URL rewrite strictly inside MK7)
struct HttpContext {
    std::string url;
    u32 method = 1;  // 1=GET, 2=POST
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    std::vector<u8> resp_body;
    std::map<std::string, std::string> resp_headers;
    u32 status_code = 0;
    u32 req_state = 1;  // 1=NotStarted, 7=ReadyToDownloadContent, 0xA=TimedOut
    size_t read_pos = 0;
};

static std::string rewrite_mk7_pretendo_url(std::string url) {
    auto rep = [&](const char *from, const char *to) {
        size_t pos = url.find(from);
        if (pos != std::string::npos) url.replace(pos, strlen(from), to);
    };
#ifdef MK7_PRETENDO_ONLINE
    // Exact Nimbus (Pretendo 3DS) HTTP domain rewrites
    rep("nintendowifi.net", "pretendo.cc");
    rep("nintendo.net", "pretendo.cc");
    rep("pokemon-gl.com", "pretendo.cc");
#endif
    // Miiverse (Pretendo Juxtaposition) for all games with Miiverse support
    rep("discovery.olv.nintendo.net", "discovery.olv.pretendo.cc");
    rep("api.olv.nintendo.net", "api.olv.pretendo.cc");
    rep("portal.olv.nintendo.net", "portal.olv.pretendo.cc");
    rep("olv.nintendo.net", "olv.pretendo.cc");
    return url;
}

struct HttpMk7 : Service {
    using Service::Service;
    std::map<u32, HttpContext> ctxs;
    u32 next_id = 1;

    static void parse_url(const std::string &u, bool &https, std::string &host, std::string &port, std::string &path) {
        https = u.rfind("https://", 0) == 0;
        size_t s = u.find("://");
        s = (s == std::string::npos) ? 0 : s + 3;
        size_t p = u.find('/', s);
        std::string hp = (p == std::string::npos) ? u.substr(s) : u.substr(s, p - s);
        path = (p == std::string::npos) ? "/" : u.substr(p);
        size_t col = hp.find(':');
        if (col != std::string::npos) {
            host = hp.substr(0, col);
            port = hp.substr(col + 1);
        } else {
            host = hp;
            port = https ? "443" : "80";
        }
    }

    static bool perform_request(HttpContext &c) {
#ifndef __WIIU__
        (void)c;
        return false;
#else
        if (g_ac.ensure()) {
            BOOL ok_ac = FALSE;
            g_ac.is_connected(&ok_ac);
            if (!ok_ac) g_ac.connect();
        }
        if (!g_net.ensure()) return false;
        bool https = false;
        std::string host, port, path;
        parse_url(rewrite_mk7_pretendo_url(c.url), https, host, port, path);
        LOG("[mk7-http] %s %s:%s%s\n", c.method == 2 ? "POST" : "GET", host.c_str(), port.c_str(), path.c_str());

        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (g_net.p_getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) return false;

        int fd = g_net.p_socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            g_net.p_freeaddrinfo(res);
            return false;
        }
        int cr = g_net.p_connect(fd, res->ai_addr, (int)res->ai_addrlen);
        g_net.p_freeaddrinfo(res);
        if (cr < 0) {
            g_net.p_socketclose(fd);
            return false;
        }

        NSSLContextHandle sctx = -1;
        NSSLConnectionHandle sconn = -1;
        if (https) {
            if (!g_net.ssl_ok) {
                g_net.p_socketclose(fd);
                return false;
            }
            sctx = g_net.p_nssl_ctx(0);
            if (sctx >= 0) {
                for (int id = 1; id <= 10; id++) g_net.p_nssl_add_pki(sctx, (NSSLServerCertId)id);
                for (int id = 100; id <= 115; id++) g_net.p_nssl_add_pki(sctx, (NSSLServerCertId)id);
                sconn = g_net.p_nssl_conn(sctx, host.c_str(), (int32_t)host.size(), 0, fd, 1);
            }
            if (sconn < 0) {
                if (sctx >= 0) g_net.p_nssl_ctx_free(sctx);
                g_net.p_socketclose(fd);
                return false;
            }
        }

        std::string req = (c.method == 2 ? "POST " : "GET ") + path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n";
        bool has_ua = false;
        for (const auto &kv : c.headers) {
            std::string kl = kv.first;
            for (char &ch : kl) ch = (char)tolower((unsigned char)ch);
            if (kl == "user-agent") has_ua = true;
            req += kv.first + ": " + kv.second + "\r\n";
        }
        if (!has_ua && host.find("pretendo.cc") != std::string::npos) {
            req += "User-Agent: CTR FPD/000B\r\n";  // 3DS genuine client User-Agent
        }
        std::string post_body = c.body;
        if (c.method == 2 && host.find("nasc.pretendo.cc") != std::string::npos) {
            load_wiiu_pnid();
            auto set_param = [&](const char *k, const std::string &v) {
                std::string key = std::string(k) + "=";
                size_t p = post_body.find(key);
                if (p == std::string::npos) {
                    if (!post_body.empty()) post_body += "&";
                    post_body += key + v;
                }
            };
            set_param("unitcd", "2");          // Retail 3DS unit code
            set_param("devname", "CTR");       // 3DS hardware
            set_param("macadr", "001B7A123456");
            set_param("userid", std::to_string(g_pnid.pid));
        }
        if (c.method == 2) req += "Content-Length: " + std::to_string(post_body.size()) + "\r\n";
        req += "\r\n";
        if (!post_body.empty()) req += post_body;

        size_t sent = 0;
        while (sent < req.size()) {
            int n = 0;
            if (https) {
                int32_t w = 0;
                if (g_net.p_nssl_write(sconn, req.data() + sent, (int32_t)(req.size() - sent), &w) != NSSL_ERROR_OK) break;
                n = w;
            } else {
                n = g_net.p_send(fd, req.data() + sent, (int)(req.size() - sent), 0);
            }
            if (n <= 0) break;
            sent += (size_t)n;
        }

        std::string raw;
        char buf[2048];
        for (;;) {
            if (!https && fd >= 0 && fd < NSYSNET_FD_SETSIZE) {
                nsysnet_fd_set rfds{};
                NSYSNET_FD_ZERO(&rfds);
                NSYSNET_FD_SET(fd, &rfds);
                nsysnet_timeval tv{10, 0};
                if (g_net.p_select(fd + 1, &rfds, nullptr, nullptr, &tv) <= 0) break;
            }
            int n = 0;
            if (https) {
                int32_t rd = 0;
                if (g_net.p_nssl_read(sconn, buf, (int32_t)sizeof(buf), &rd) != NSSL_ERROR_OK) break;
                n = rd;
            } else {
                n = g_net.p_recv(fd, buf, sizeof(buf), 0);
            }
            if (n <= 0) break;
            raw.append(buf, (size_t)n);
        }

        if (https) {
            g_net.p_nssl_conn_free(sconn);
            g_net.p_nssl_ctx_free(sctx);
        }
        g_net.p_socketclose(fd);

        size_t hdr_end = raw.find("\r\n\r\n");
        if (hdr_end == std::string::npos) return false;
        std::string hdrs = raw.substr(0, hdr_end);
        c.resp_body.assign(raw.begin() + hdr_end + 4, raw.end());
        c.read_pos = 0;
        size_t sp = hdrs.find(' ');
        c.status_code = sp != std::string::npos ? (u32)atoi(hdrs.c_str() + sp + 1) : 200;
        size_t pos = hdrs.find("\r\n");
        while (pos != std::string::npos) {
            size_t next = hdrs.find("\r\n", pos + 2);
            std::string line = hdrs.substr(pos + 2, next == std::string::npos ? std::string::npos : next - (pos + 2));
            size_t col = line.find(':');
            if (col != std::string::npos) {
                std::string k = line.substr(0, col);
                for (char &ch : k) ch = (char)tolower((unsigned char)ch);
                std::string v = line.substr(col + 1);
                while (!v.empty() && v[0] == ' ') v.erase(v.begin());
                c.resp_headers[k] = v;
            }
            pos = next;
        }
        return true;
#endif
    }

    void handle(IPC &ipc) override {
        switch (ipc.cmd()) {
        case 0x0001:  // Initialize
        case 0x0008:  // InitializeConnectionSession
        case 0x0013:  // SetSSLOpt
        case 0x0016:  // SetClientCertDefault
        case 0x0024:  // SetKeepAlive
            ipc.ok();
            break;
        case 0x0002: {  // CreateContext(url_len, method, url_buf)
            u32 len = ipc[1], method = ipc[2], ptr = ipc[4];
            u32 id = next_id++;
            HttpContext &c = ctxs[id];
            c.url.assign((const char *)gptr(ptr), strnlen((const char *)gptr(ptr), len));
            c.method = method;
            ipc.ok(2);
            ipc.set(2, id);
            break;
        }
        case 0x0003:  // CloseContext
            ctxs.erase(ipc[1]);
            ipc.ok();
            break;
        case 0x0004:  // CancelConnection
            ipc.ok();
            break;
        case 0x0005: {  // GetRequestState
            auto it = ctxs.find(ipc[1]);
            ipc.ok(2);
            ipc.set(2, it != ctxs.end() ? it->second.req_state : 1u);
            break;
        }
        case 0x0006: {  // GetDownloadSizeState
            auto it = ctxs.find(ipc[1]);
            u32 pos = it != ctxs.end() ? (u32)it->second.read_pos : 0;
            u32 tot = it != ctxs.end() ? (u32)it->second.resp_body.size() : 0;
            ipc.ok(3);
            ipc.set(2, pos);
            ipc.set(3, tot);
            break;
        }
        case 0x0007:  // GetRequestError
            ipc.ok(2);
            ipc.set(2, 0);
            break;
        case 0x0009:  // BeginRequest
        case 0x000A: {  // BeginRequestAsync
            u32 h = ipc[1];
            auto it = ctxs.find(h);
            if (it == ctxs.end()) {
                ipc.result(0xD8A0A040);
                break;
            }
            HttpContext tmp = it->second;
            bool ok = false;
            run_io(ipc.lk, [&] { ok = perform_request(tmp); });
            tmp.req_state = ok ? 7u : 0xAu;
            it = ctxs.find(h);
            if (it != ctxs.end()) it->second = std::move(tmp);
            if (!ok) {
                ipc.result(0xD8A0A040);
                break;
            }
            ipc.ok();
            break;
        }
        case 0x000B:    // ReceiveData
        case 0x000C:    // ReceiveDataTimeout
        case 0x0020:    // GetResponseData
        case 0x0021: {  // GetResponseDataTimeout
            auto it = ctxs.find(ipc[1]);
            if (it == ctxs.end()) { ipc.result(0xD8A0A040); break; }
            HttpContext &c = it->second;
            u32 size = ipc[2];
            u32 dst = (ipc.cmd() == 0x000C || ipc.cmd() == 0x0021) ? ipc[6] : ipc[4];
            size_t avail = c.resp_body.size() - c.read_pos;
            size_t take = std::min<size_t>(size, avail);
            if (take && dst) {
                memcpy(gptr(dst), c.resp_body.data() + c.read_pos, take);
                c.read_pos += take;
            }
            ipc.ok();
            break;
        }
        case 0x0011: {  // AddRequestHeader
            auto it = ctxs.find(ipc[1]);
            if (it != ctxs.end()) {
                std::string k((const char *)gptr(ipc[5]), strnlen((const char *)gptr(ipc[5]), ipc[2]));
                std::string v((const char *)gptr(ipc[7]), strnlen((const char *)gptr(ipc[7]), ipc[3]));
                it->second.headers.emplace_back(k, v);
            }
            ipc.ok();
            break;
        }
        case 0x0012: {  // AddPostDataAscii
            auto it = ctxs.find(ipc[1]);
            if (it != ctxs.end()) {
                std::string k((const char *)gptr(ipc[5]), strnlen((const char *)gptr(ipc[5]), ipc[2]));
                std::string v((const char *)gptr(ipc[7]), strnlen((const char *)gptr(ipc[7]), ipc[3]));
                if (!it->second.body.empty()) it->second.body += "&";
                it->second.body += k + "=" + v;
            }
            ipc.ok();
            break;
        }
        case 0x0014: {  // AddPostDataRaw
            auto it = ctxs.find(ipc[1]);
            if (it != ctxs.end()) it->second.body.append((const char *)gptr(ipc[4]), ipc[2]);
            ipc.ok();
            break;
        }
        case 0x001E:    // GetResponseHeader
        case 0x001F: {  // GetResponseHeaderTimeout
            auto it = ctxs.find(ipc[1]);
            const bool to = (ipc.cmd() == 0x001F);
            u32 kva = to ? ipc[7] : ipc[5];
            u32 vva = to ? ipc[9] : ipc[7];
            std::string k(kva ? (const char *)gptr(kva) : "", kva ? strnlen((const char *)gptr(kva), ipc[2]) : 0);
            for (char &ch : k) ch = (char)tolower((unsigned char)ch);
            std::string v;
            if (it != ctxs.end()) {
                auto hi = it->second.resp_headers.find(k);
                if (hi != it->second.resp_headers.end()) v = hi->second;
            }
            u32 copy = std::min<u32>((u32)v.size(), ipc[3]);
            if (copy && vva) memcpy(gptr(vva), v.data(), copy);
            ipc.ok(2);
            ipc.set(2, (u32)v.size());
            break;
        }
        case 0x0022:    // GetResponseStatusCode
        case 0x0023: {  // GetResponseStatusCodeTimeout
            auto it = ctxs.find(ipc[1]);
            ipc.ok(2);
            ipc.set(2, it != ctxs.end() ? it->second.status_code : 0);
            break;
        }
        default:
            unimplemented(ipc);
        }
    }
};

// ---- act:u (exposes Wii U's active Pretendo PNID to MK7 / Miiverse)
struct ActMk7 : Service {
    using Service::Service;
    void handle(IPC &ipc) override {
#ifdef __WIIU__
        load_wiiu_pnid();
#endif
        switch (ipc.cmd()) {
        case 0x0001:  // Initialize
            ipc.ok();
            break;
        case 0x0006: {  // GetAccountInfo(slot, size, info_type, buf)
            const u32 size = ipc[2], type = ipc[3], dst = ipc[5];
            u8 *p = gptr(dst);
            memset(p, 0, size);
            if (type == 0x05 && size >= 4) le32(p, g_pnid.pid);                      // PrincipalId
            else if (type == 0x06 && size >= 1) p[0] = g_pnid.from_wiiu ? 1 : 0;      // IsNetworkAccount
            else if (type == 0x08 && size >= 17) memcpy(p, g_pnid.account_id, 17);    // NNID / PNID string
            else if (type == 0x1B && size >= 22) memcpy(p, g_pnid.mii_name, 22);      // Mii name UTF-16LE
            else if (type == 0x17 && size >= 0x60) memcpy(p, g_pnid.mii.data(), 0x60);
            ipc.ok();
            break;
        }
        default:
            ipc.ok(2);
            ipc.set(2, 0);
            break;
        }
    }
};

struct GenericNetStub : Service {
    using Service::Service;
    void handle(IPC &ipc) override { unimplemented(ipc); }
};

}  // namespace

void miiverse_open_wiiu() {
#ifdef __WIIU__
    OSDynLoad_Module m{};
    if (OSDynLoad_Acquire("sysapp.rpl", &m) != OS_DYNLOAD_OK) return;
    decltype(&SYSSwitchToBrowser) pSwitch = nullptr;
    if (OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, "SYSSwitchToBrowser", (void **)&pSwitch) != OS_DYNLOAD_OK || !pSwitch) return;
    static char url[] = "https://juxtaposition.pretendo.cc";
    SysAppBrowserArgs args{};
    args.url = url;
    args.urlSize = sizeof(url);
    pSwitch(&args);
#endif
}

void register_mk7_online() {
#ifdef MK7_PRETENDO_ONLINE
    register_service(std::make_shared<AcMk7>("ac:u"));
    register_service(std::make_shared<AcMk7>("ac:i"));
    register_service(std::make_shared<FrdMk7>("frd:u"));
    register_service(std::make_shared<FrdMk7>("frd:a"));
    register_service(std::make_shared<SocMk7>("soc:U"));
#else
    register_service(std::make_shared<GenericNetStub>("soc:U"));  // fallback stub outside MK7
#endif
    register_service(std::make_shared<ActMk7>("act:u"));
    register_service(std::make_shared<HttpMk7>("http:C"));
}

}  // namespace kernel
