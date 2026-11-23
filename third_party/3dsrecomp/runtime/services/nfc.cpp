// nfc:u / nfc:m: 3DS Amiibo / NFC service routed to the Wii U GamePad's nn::nfp (nn_nfp.rpl).
// Loaded dynamically via OSDynLoad_Acquire so games that never touch NFC add no static RPL import.
#include <algorithm>
#include <atomic>
#include <cstring>

#include "../service.h"
#include "services.h"

#ifdef __WIIU__
#include <coreinit/dynload.h>
#include <coreinit/event.h>
#include <coreinit/thread.h>
#include <nn/nfp/nfp_cpp.h>
#endif

namespace kernel {

namespace {

enum class TagState : u8 {
    NotInitialized = 0,
    NotScanning = 1,
    Scanning = 2,
    TagInRange = 3,
    TagOutOfRange = 4,
    TagDataLoaded = 5,
    Unknown6 = 6,
};

constexpr u32 RES_NFC_INVALID_STATE = 0xC8A17600;
constexpr u32 RES_NFC_APP_AREA_MISSING = 0xC8A17628;
constexpr u32 RES_NFC_NO_REGISTER_INFO = 0xC8A17638;
constexpr u32 RES_NFC_INVALID_TAG = 0xC8A17610;
constexpr u32 RES_NFC_GENERIC_FAIL = 0xD8E17400;

inline void put_le16(u8 *p, u16 v) {
    p[0] = (u8)(v & 0xFF);
    p[1] = (u8)((v >> 8) & 0xFF);
}

inline void put_le32(u8 *p, u32 v) {
    p[0] = (u8)(v & 0xFF);
    p[1] = (u8)((v >> 8) & 0xFF);
    p[2] = (u8)((v >> 16) & 0xFF);
    p[3] = (u8)((v >> 24) & 0xFF);
}

inline void put_le64(u8 *p, u64 v) {
    for (int i = 0; i < 8; i++) p[i] = (u8)((v >> (i * 8)) & 0xFF);
}

struct NfcState {
    std::shared_ptr<Event> tag_in_range = make_event(ONESHOT, "NFC::tag_in_range");
    std::shared_ptr<Event> tag_out_of_range = make_event(ONESHOT, "NFC::tag_out_of_range");
    TagState state = TagState::NotInitialized;
    bool initialized = false;
    u32 app_area_size = 0;
#ifdef __WIIU__
    bool rpl_tried = false;
    bool rpl_ok = false;
    bool app_area_open = false;
    std::atomic<bool> watch_running{false};
    OSEvent activate_ev{};
    OSEvent deactivate_ev{};
    OSThread watch_thread{};
    alignas(16) u8 watch_stack[0x8000]{};

    decltype(&nn::nfp::Initialize) pInitialize = nullptr;
    decltype(&nn::nfp::Finalize) pFinalize = nullptr;
    decltype(&nn::nfp::GetNfpState) pGetNfpState = nullptr;
    decltype(&nn::nfp::SetActivateEvent) pSetActivateEvent = nullptr;
    decltype(&nn::nfp::SetDeactivateEvent) pSetDeactivateEvent = nullptr;
    decltype(&nn::nfp::StartDetection) pStartDetection = nullptr;
    decltype(&nn::nfp::StopDetection) pStopDetection = nullptr;
    decltype(&nn::nfp::Mount) pMount = nullptr;
    decltype(&nn::nfp::MountReadOnly) pMountReadOnly = nullptr;
    decltype(&nn::nfp::MountRom) pMountRom = nullptr;
    decltype(&nn::nfp::Unmount) pUnmount = nullptr;
    decltype(&nn::nfp::Flush) pFlush = nullptr;
    decltype(&nn::nfp::Format) pFormat = nullptr;
    decltype(&nn::nfp::IsExistApplicationArea) pIsExistAppArea = nullptr;
    decltype(&nn::nfp::CreateApplicationArea) pCreateAppArea = nullptr;
    decltype(&nn::nfp::OpenApplicationArea) pOpenAppArea = nullptr;
    decltype(&nn::nfp::ReadApplicationArea) pReadAppArea = nullptr;
    decltype(&nn::nfp::WriteApplicationArea) pWriteAppArea = nullptr;
    decltype(&nn::nfp::GetNfpRegisterInfo) pGetRegisterInfo = nullptr;
    decltype(&nn::nfp::GetNfpCommonInfo) pGetCommonInfo = nullptr;
    decltype(&nn::nfp::GetNfpReadOnlyInfo) pGetReadOnlyInfo = nullptr;
    decltype(&nn::nfp::GetNfpAdminInfo) pGetAdminInfo = nullptr;
    decltype(&nn::nfp::GetTagInfo) pGetTagInfo = nullptr;

    bool load_rpl() {
        if (rpl_tried) return rpl_ok;
        rpl_tried = true;
        OSDynLoad_Module m{};
        if (OSDynLoad_Acquire("nn_nfp.rpl", &m) != OS_DYNLOAD_OK) {
            LOG("[nfc] nn_nfp.rpl didn't load\n");
            return false;
        }
        struct Sym { const char *name; void **dst; };
        const Sym syms[] = {
            {"Initialize__Q2_2nn3nfpFv", (void **)&pInitialize},
            {"Finalize__Q2_2nn3nfpFv", (void **)&pFinalize},
            {"GetNfpState__Q2_2nn3nfpFv", (void **)&pGetNfpState},
            {"SetActivateEvent__Q2_2nn3nfpFP7OSEvent", (void **)&pSetActivateEvent},
            {"SetDeactivateEvent__Q2_2nn3nfpFP7OSEvent", (void **)&pSetDeactivateEvent},
            {"StartDetection__Q2_2nn3nfpFv", (void **)&pStartDetection},
            {"StopDetection__Q2_2nn3nfpFv", (void **)&pStopDetection},
            {"Mount__Q2_2nn3nfpFv", (void **)&pMount},
            {"MountReadOnly__Q2_2nn3nfpFv", (void **)&pMountReadOnly},
            {"MountRom__Q2_2nn3nfpFv", (void **)&pMountRom},
            {"Unmount__Q2_2nn3nfpFv", (void **)&pUnmount},
            {"Flush__Q2_2nn3nfpFv", (void **)&pFlush},
            {"Format__Q2_2nn3nfpFPCUci", (void **)&pFormat},
            {"IsExistApplicationArea__Q2_2nn3nfpFv", (void **)&pIsExistAppArea},
            {"CreateApplicationArea__Q2_2nn3nfpFRCQ3_2nn3nfp25ApplicationAreaCreateInfo", (void **)&pCreateAppArea},
            {"OpenApplicationArea__Q2_2nn3nfpFUi", (void **)&pOpenAppArea},
            {"ReadApplicationArea__Q2_2nn3nfpFPvUi", (void **)&pReadAppArea},
            {"WriteApplicationArea__Q2_2nn3nfpFPCvUiRCQ3_2nn3nfp5TagId", (void **)&pWriteAppArea},
            {"GetNfpRegisterInfo__Q2_2nn3nfpFPQ3_2nn3nfp12RegisterInfo", (void **)&pGetRegisterInfo},
            {"GetNfpCommonInfo__Q2_2nn3nfpFPQ3_2nn3nfp10CommonInfo", (void **)&pGetCommonInfo},
            {"GetNfpReadOnlyInfo__Q2_2nn3nfpFPQ3_2nn3nfp12ReadOnlyInfo", (void **)&pGetReadOnlyInfo},
            {"GetNfpAdminInfo__Q2_2nn3nfpFPQ3_2nn3nfp9AdminInfo", (void **)&pGetAdminInfo},
            {"GetTagInfo__Q2_2nn3nfpFPQ3_2nn3nfp7TagInfo", (void **)&pGetTagInfo},
        };
        for (const auto &s : syms) {
            if (OSDynLoad_FindExport(m, OS_DYNLOAD_EXPORT_FUNC, s.name, s.dst) != OS_DYNLOAD_OK || !*s.dst) {
                LOG("[nfc] missing nn_nfp symbol: %s\n", s.name);
                return false;
            }
        }
        rpl_ok = true;
        return true;
    }

    static u32 map_nfp_err(nn::Result r) {
        if (r.IsSuccess()) return RES_OK;
        const u32 desc = (u32)(static_cast<NNResult>(r).value & 0xFFFFF);
        if (desc == nn::nfp::RESULT_APP_AREA_MISSING) return RES_NFC_APP_AREA_MISSING;
        if (desc == nn::nfp::RESULT_NO_REGISTER_INFO) return RES_NFC_NO_REGISTER_INFO;
        if (desc == nn::nfp::RESULT_INVALID_STATE) return RES_NFC_INVALID_STATE;
        if (desc == nn::nfp::RESULT_INVALID_TAG || desc == nn::nfp::RESULT_INVALID_TAG_INFO) return RES_NFC_INVALID_TAG;
        return RES_NFC_GENERIC_FAIL;
    }

    TagState current_state() {
        if (!rpl_ok || !pGetNfpState) return state;
        switch (pGetNfpState()) {
        case nn::nfp::NfpState::Uninitialized: return TagState::NotInitialized;
        case nn::nfp::NfpState::Initialized: return TagState::NotScanning;
        case nn::nfp::NfpState::Searching: return TagState::Scanning;
        case nn::nfp::NfpState::Found: return TagState::TagInRange;
        case nn::nfp::NfpState::Removed: return TagState::TagOutOfRange;
        case nn::nfp::NfpState::Mounted:
        case nn::nfp::NfpState::MountedROM: return TagState::TagDataLoaded;
        case nn::nfp::NfpState::Unknown6: return TagState::Unknown6;
        default: return state;
        }
    }
#endif
};

NfcState g_nfc;

#ifdef __WIIU__
static int nfc_watch_entry(int, void *) {
    nn::nfp::NfpState prev = nn::nfp::NfpState::Uninitialized;
    while (g_nfc.watch_running.load(std::memory_order_relaxed)) {
        if (g_nfc.rpl_ok && g_nfc.pGetNfpState) {
            const nn::nfp::NfpState cur = g_nfc.pGetNfpState();
            if (cur != prev) {
                if (cur == nn::nfp::NfpState::Found && prev != nn::nfp::NfpState::Mounted && prev != nn::nfp::NfpState::MountedROM) {
                    run_when_quiescent([] { g_nfc.tag_in_range->signal(); });
                } else if (cur == nn::nfp::NfpState::Removed) {
                    g_nfc.app_area_open = false;
                    run_when_quiescent([] { g_nfc.tag_out_of_range->signal(); });
                }
                prev = cur;
            }
        }
        OSSleepTicks(OSMillisecondsToTicks(16));
    }
    return 0;
}

static void start_nfc_watcher() {
    if (g_nfc.watch_running.exchange(true)) return;
    OSInitEvent(&g_nfc.activate_ev, FALSE, OS_EVENT_MODE_AUTO);
    OSInitEvent(&g_nfc.deactivate_ev, FALSE, OS_EVENT_MODE_AUTO);
    if (g_nfc.pSetActivateEvent) g_nfc.pSetActivateEvent(&g_nfc.activate_ev);
    if (g_nfc.pSetDeactivateEvent) g_nfc.pSetDeactivateEvent(&g_nfc.deactivate_ev);
    if (OSCreateThread(&g_nfc.watch_thread, nfc_watch_entry, 0, nullptr,
                       g_nfc.watch_stack + sizeof(g_nfc.watch_stack), sizeof(g_nfc.watch_stack),
                       20, OS_THREAD_ATTRIB_AFFINITY_CPU0)) {
        OSSetThreadName(&g_nfc.watch_thread, "3dii-u nfc");
        OSResumeThread(&g_nfc.watch_thread);
    } else {
        g_nfc.watch_running = false;
    }
}

static void stop_nfc_watcher() {
    if (!g_nfc.watch_running.exchange(false)) return;
    OSSignalEvent(&g_nfc.activate_ev);
    OSSignalEvent(&g_nfc.deactivate_ev);
    OSJoinThread(&g_nfc.watch_thread, nullptr);
}
#endif

struct Nfc : Service {
    using Service::Service;
    void handle(IPC &ipc) override {
        switch (ipc.cmd()) {
        case 0x0001: {  // Initialize(u8 param)
            if (g_nfc.initialized) {
                ipc.result(RES_NFC_INVALID_STATE);
                break;
            }
#ifdef __WIIU__
            if (g_nfc.load_rpl()) {
                nn::Result r = g_nfc.pInitialize();
                if (r.IsFailure()) {
                    LOG("[nfc] nn::nfp::Initialize failed: %08x\n", (unsigned)static_cast<NNResult>(r).value);
                    ipc.result(NfcState::map_nfp_err(r));
                    break;
                }
                start_nfc_watcher();
            }
#endif
            g_nfc.initialized = true;
            g_nfc.state = TagState::NotScanning;
            LOG("[nfc] Initialize(%u)\n", ipc[1] & 0xFF);
            ipc.ok();
            break;
        }
        case 0x0002: {  // Shutdown(u8 param)
#ifdef __WIIU__
            stop_nfc_watcher();
            if (g_nfc.rpl_ok && g_nfc.pFinalize) g_nfc.pFinalize();
            g_nfc.app_area_open = false;
#endif
            g_nfc.initialized = false;
            g_nfc.state = TagState::NotInitialized;
            ipc.ok();
            break;
        }
        case 0x0003:  // StartCommunication
        case 0x0004:  // StopCommunication
            ipc.ok();
            break;
        case 0x0005:    // StartTagScanning(u16)
        case 0x001F: {  // StartOtherTagScanning(u16, u32)
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pStartDetection) {
                nn::Result r = g_nfc.pStartDetection();
                if (r.IsFailure()) {
                    ipc.result(NfcState::map_nfp_err(r));
                    break;
                }
            }
#endif
            g_nfc.state = TagState::Scanning;
            ipc.ok();
            break;
        }
        case 0x0006: {  // StopTagScanning
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pStopDetection) {
                g_nfc.pStopDetection();
                g_nfc.app_area_open = false;
            }
#endif
            g_nfc.state = TagState::NotScanning;
            ipc.ok();
            break;
        }
        case 0x0007: {  // LoadAmiiboData
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pMount) {
                nn::Result r = g_nfc.pMount();
                if (r.IsFailure() && g_nfc.pMountReadOnly) r = g_nfc.pMountReadOnly();
                if (r.IsFailure()) {
                    ipc.result(NfcState::map_nfp_err(r));
                    break;
                }
                nn::nfp::CommonInfo ci{};
                if (g_nfc.pGetCommonInfo && g_nfc.pGetCommonInfo(&ci).IsSuccess()) {
                    g_nfc.app_area_size = ci.applicationAreaSize;
                }
            }
#endif
            g_nfc.state = TagState::TagDataLoaded;
            ipc.ok();
            break;
        }
        case 0x0008: {  // ResetTagScanState
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pUnmount) {
                g_nfc.pUnmount();
                g_nfc.app_area_open = false;
            }
#endif
            g_nfc.state = TagState::TagInRange;
            ipc.ok();
            break;
        }
        case 0x0009: {  // UpdateStoredAmiiboData
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pFlush) {
                nn::Result r = g_nfc.pFlush();
                if (r.IsFailure()) {
                    ipc.result(NfcState::map_nfp_err(r));
                    break;
                }
            }
#endif
            ipc.ok();
            break;
        }
        case 0x000B:  // GetTagInRangeEvent
            ipc.ok(1, 2);
            ipc.handles(2, {g_nfc.tag_in_range});
            break;
        case 0x000C:  // GetTagOutOfRangeEvent
            ipc.ok(1, 2);
            ipc.handles(2, {g_nfc.tag_out_of_range});
            break;
        case 0x000D: {  // GetTagState
#ifdef __WIIU__
            const TagState st = g_nfc.current_state();
#else
            const TagState st = g_nfc.state;
#endif
            ipc.ok(2);
            ipc.set(2, (u32)st);
            break;
        }
        case 0x000F:  // CommunicationGetStatus
            ipc.ok(2);
            ipc.set(2, g_nfc.initialized ? 2u : 0u);  // NfcInitialized
            break;
        case 0x0010: {  // GetTagInfo2 (0x54 bytes)
            u8 out[0x54]{};
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pGetTagInfo) {
                nn::nfp::TagInfo ti{};
                nn::Result r = g_nfc.pGetTagInfo(&ti);
                if (r.IsFailure()) {
                    ipc.result(NfcState::map_nfp_err(r));
                    break;
                }
                out[0] = ti.id.size;
                memcpy(out + 1, ti.id.uid, std::min<size_t>(ti.id.size, 10));
                out[0x20] = ti.technology;
                out[0x21] = (u8)ti.tag_type;
            }
#endif
            ipc.ok(22);
            memcpy(gptr(ipc.buf + 8), out, sizeof(out));
            break;
        }
        case 0x0011: {  // GetTagInfo (0x2C bytes)
            u8 out[0x2C]{};
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pGetTagInfo) {
                nn::nfp::TagInfo ti{};
                nn::Result r = g_nfc.pGetTagInfo(&ti);
                if (r.IsFailure()) {
                    ipc.result(NfcState::map_nfp_err(r));
                    break;
                }
                put_le16(out + 0x00, ti.id.size);
                out[0x02] = 0;  // protocol
                out[0x03] = (u8)ti.tag_type;
                memcpy(out + 0x04, ti.id.uid, std::min<size_t>(ti.id.size, 7));
            }
#endif
            ipc.ok(12);
            memcpy(gptr(ipc.buf + 8), out, sizeof(out));
            break;
        }
        case 0x0012:  // CommunicationGetResult
            ipc.ok(2);
            ipc.set(2, 0);
            break;
        case 0x0013: {  // OpenAppData(u32 access_id)
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pOpenAppArea) {
                nn::Result r = g_nfc.pOpenAppArea(ipc[1]);
                if (r.IsFailure()) {
                    ipc.result(NfcState::map_nfp_err(r));
                    break;
                }
                g_nfc.app_area_open = true;
            }
#endif
            ipc.ok();
            break;
        }
        case 0x0014: {  // InitializeWriteAppData(u32 access_id, u32 size, mapped buf...)
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pCreateAppArea) {
                const u32 access_id = ipc[1];
                const u32 size = ipc[2];
                const u32 buf = ipc[16] ? ipc[16] : ipc[14];
                nn::nfp::ApplicationAreaCreateInfo ci{};
                ci.accessID = access_id;
                ci.data = buf ? gptr(buf) : nullptr;
                ci.size = size;
                nn::Result r = g_nfc.pCreateAppArea(ci);
                if (r.IsFailure()) {
                    ipc.result(NfcState::map_nfp_err(r));
                    break;
                }
                g_nfc.app_area_size = size;
                g_nfc.app_area_open = true;
            }
#endif
            ipc.ok();
            break;
        }
        case 0x0015: {  // ReadAppData(u32 size, mapped buf)
            const u32 size = ipc[1];
            const u32 dst = ipc[3];
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pReadAppArea) {
                if (!g_nfc.app_area_open) {
                    ipc.result(RES_NFC_APP_AREA_MISSING);
                    break;
                }
                nn::Result r = g_nfc.pReadAppArea(gptr(dst), size);
                if (r.IsFailure()) {
                    ipc.result(NfcState::map_nfp_err(r));
                    break;
                }
            }
#else
            memset(gptr(dst), 0, size);
#endif
            ipc.ok(1, 2);
            ipc.set(2, ipc[2]);
            ipc.set(3, dst);
            break;
        }
        case 0x0016: {  // WriteAppData(u32 size, tag_id..., mapped buf)
            const u32 size = ipc[1];
            const u32 src = ipc[11];
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pWriteAppArea) {
                nn::nfp::TagInfo ti{};
                if (g_nfc.pGetTagInfo) g_nfc.pGetTagInfo(&ti);
                nn::Result r = g_nfc.pWriteAppArea(gptr(src), size, ti.id);
                if (r.IsFailure()) {
                    ipc.result(NfcState::map_nfp_err(r));
                    break;
                }
            }
#else
            (void)size;
            (void)src;
#endif
            ipc.ok();
            break;
        }
        case 0x0017: {  // GetAmiiboSettings (0xA8 bytes RegisterInfo)
            u8 out[0xA8]{};
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pGetRegisterInfo) {
                nn::nfp::RegisterInfo ri{};
                nn::Result r = g_nfc.pGetRegisterInfo(&ri);
                if (r.IsFailure()) {
                    ipc.result(NfcState::map_nfp_err(r));
                    break;
                }
                std::array<u8, 0x60> host_mii{}, guest_mii{};
                memcpy(host_mii.data(), &ri.mii, 0x60);
                if (mii_3ds(host_mii, guest_mii)) memcpy(out, guest_mii.data(), 0x60);
                else memcpy(out, &ri.mii, 0x60);
                for (int i = 0; i < 11; i++) put_le16(out + 0x60 + i * 2, ri.name[i]);
                out[0x76] = ri.fontRegion;
                out[0x77] = ri.country;
                put_le16(out + 0x78, ri.registerDate.year);
                out[0x7A] = ri.registerDate.month;
                out[0x7B] = ri.registerDate.day;
            }
#endif
            ipc.ok(43);
            memcpy(gptr(ipc.buf + 8), out, sizeof(out));
            break;
        }
        case 0x0018: {  // GetAmiiboConfig (0x40 bytes CommonInfo)
            u8 out[0x40]{};
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pGetCommonInfo) {
                nn::nfp::CommonInfo ci{};
                nn::Result r = g_nfc.pGetCommonInfo(&ci);
                if (r.IsFailure()) {
                    ipc.result(NfcState::map_nfp_err(r));
                    break;
                }
                put_le16(out + 0x00, ci.lastWriteDate.year);
                out[0x02] = ci.lastWriteDate.month;
                out[0x03] = ci.lastWriteDate.day;
                put_le16(out + 0x04, ci.writes);
                memcpy(out + 0x06, ci.characterID, 3);
                out[0x09] = ci.seriesID;
                put_le16(out + 0x0A, __builtin_bswap16(ci.numberingID));  // big-endian in 3DS CommonInfo
                out[0x0C] = ci.figureType;
                out[0x0D] = ci.figureVersion;
                put_le16(out + 0x0E, ci.applicationAreaSize);
            }
#endif
            ipc.ok(17);
            memcpy(gptr(ipc.buf + 8), out, sizeof(out));
            break;
        }
        case 0x0019: {  // GetAppDataInitStruct (0x3C bytes zeroed)
            ipc.ok(16);
            memset(gptr(ipc.buf + 8), 0, 0x3C);
            break;
        }
        case 0x001A: {  // MountRom
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pMountRom) {
                nn::Result r = g_nfc.pMountRom();
                if (r.IsFailure()) {
                    ipc.result(NfcState::map_nfp_err(r));
                    break;
                }
            }
#endif
            g_nfc.state = TagState::Unknown6;
            ipc.ok();
            break;
        }
        case 0x001B: {  // GetIdentificationBlock (0x36 bytes)
            u8 out[0x36]{};
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pGetReadOnlyInfo) {
                nn::nfp::ReadOnlyInfo roi{};
                nn::Result r = g_nfc.pGetReadOnlyInfo(&roi);
                if (r.IsFailure()) {
                    ipc.result(NfcState::map_nfp_err(r));
                    break;
                }
                memcpy(out + 0x00, roi.characterID, 3);
                out[0x03] = roi.seriesID;
                put_le16(out + 0x04, __builtin_bswap16(roi.numberingID));
                out[0x06] = roi.figureType;
            }
#endif
            ipc.ok(0x0F);
            memcpy(gptr(ipc.buf + 8), out, sizeof(out));
            break;
        }
        case 0x0401: {  // nfc:m Format
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pFormat) {
                nn::Result r = g_nfc.pFormat(nullptr, 0);
                if (r.IsFailure()) {
                    ipc.result(NfcState::map_nfp_err(r));
                    break;
                }
            }
#endif
            ipc.ok();
            break;
        }
        case 0x0404: {  // nfc:m GetAdminInfo (0x40 bytes)
            u8 out[0x40]{};
#ifdef __WIIU__
            if (g_nfc.rpl_ok && g_nfc.pGetAdminInfo) {
                nn::nfp::AdminInfo ai{};
                nn::Result r = g_nfc.pGetAdminInfo(&ai);
                if (r.IsFailure()) {
                    ipc.result(NfcState::map_nfp_err(r));
                    break;
                }
                put_le64(out + 0x00, ai.titleID);
                put_le32(out + 0x08, ai.accessID);
                put_le16(out + 0x0C, ai.applicationAreaWrites);
                out[0x0E] = (u8)ai.flags;
                out[0x0F] = ai.formatVersion;
                out[0x10] = ai.platform;
            }
#endif
            ipc.ok(17);
            memcpy(gptr(ipc.buf + 8), out, sizeof(out));
            break;
        }
        default:
            unimplemented(ipc);
            break;
        }
    }
};

}  // namespace

void register_nfc() {
    register_service(std::make_shared<Nfc>("nfc:u"));
    register_service(std::make_shared<Nfc>("nfc:m"));
}

}  // namespace kernel
