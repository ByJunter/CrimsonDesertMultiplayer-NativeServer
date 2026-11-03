#pragma once
// Option D addresses, game build 1.0.0.2976 only (RVA from the image base).
// Sources: docs/research/opcion-d-d0.md (stage D0), segunda-sesion.md,
// troc-dispatch.md. Every function the mod calls or hooks is checked against
// its first bytes before use; a mismatch leaves option D off.

#include <cstdint>

namespace cdmp::session2::off {

// Globals (pointers to the objects).
inline constexpr uintptr_t kServerNetSlot = 0x6D69680;  // [exe+..] = ServerNet
inline constexpr uintptr_t kClientNetSlot = 0x6D69170;  // [exe+..] = ClientNet
inline constexpr uintptr_t kSamSlot = 0x6DA67D8;        // [exe+..] = ServerActorManager

// ServerNet fields.
inline constexpr uint32_t kServerNet_Session = 0x18;  // the only NwVirtualAsyncSession
inline constexpr uint32_t kServerNet_InQueue = 0x20;  // Q0: C->S buffers, popped by ServerRunThread
inline constexpr uint32_t kServerNet_FreeC2S = 0x78;  // Q3: C->S buffers back to the client
inline constexpr uint32_t kServerNet_FreeS2C = 0x80;  // Q2: S->C buffers back to the server
// ClientNet fields.
inline constexpr uint32_t kClientNet_InQueue = 0x18;  // Q1 (== session+0xC8)
// ServerActorManager fields.
inline constexpr uint32_t kSam_FreeKeys = 0xD0;     // u32: keys left in the common queue (15501 at most)
inline constexpr uint32_t kSam_ActorCount = 0x9C;   // u32

// NwVirtualAsyncSession (0xD0 bytes).
inline constexpr uintptr_t kNwVirtualAsyncSessionVtable = 0x57260C0;  // 15 entries, COL at [-1]
inline constexpr uintptr_t kNwAsyncSessionVtable = 0x5726140;
inline constexpr uint32_t kSessionVtableEntries = 15;
inline constexpr uint32_t kSessionVfSend = 9;
inline constexpr uint32_t kSessionSize = 0xD0;
inline constexpr uint32_t kSession_Name = 0x18;     // u16 short string: "1" for the host
inline constexpr uint32_t kSession_IdPtr = 0x88;    // u32*: key of the deferred queue until a user exists
inline constexpr uint32_t kSession_User = 0x90;     // ServerUserActor*
inline constexpr uint32_t kSession_Crypto = 0x98;   // CryptogramAesGcm*; byte +0x24 = 0 (off)
inline constexpr uint32_t kSession_B0 = 0xB0;
inline constexpr uint32_t kSession_B8 = 0xB8;
inline constexpr uint32_t kSession_C0 = 0xC0;       // u16
inline constexpr uint32_t kSession_OutQueue = 0xC8;

// ServerUserActor (0x218 bytes).
inline constexpr uintptr_t kServerUserActorVtable = 0x5B257A8;
inline constexpr uint32_t kUser_Key = 0x60;
inline constexpr uint32_t kUser_Session = 0x1F8;
// Any server actor: the key of the user whose client controls it (D0 §7.3).
inline constexpr uint32_t kServerActor_Controller = 0x90;
inline constexpr uint32_t kServerActor_Key = 0x60;

// Functions.
inline constexpr uintptr_t kServerOnRecvPacket = 0x2773C20;  // (ServerNet*, int*, NwSession*, u8* pkt, u16 size, u8 channel)
inline constexpr uint8_t kServerOnRecvPacketHead[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x4C, 0x89, 0x4C, 0x24, 0x20,
                                                      0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54};
inline constexpr uintptr_t kSessionSend = 0x1437740;  // vf9 (this, int* res, AsyncRequestSend* rq) -> res
inline constexpr uint8_t kSessionSendHead[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20,
                                               0x48, 0x8B, 0x89, 0xC8, 0x00, 0x00, 0x00, 0x49, 0x8D, 0x40, 0x30};
inline constexpr uintptr_t kNwSessionCtor = 0x1436EE0;  // (this, bool) -> this
inline constexpr uint8_t kNwSessionCtorHead[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x20,
                                                 0x48, 0x89, 0x4C, 0x24, 0x08, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56};
inline constexpr uintptr_t kQueuePush = 0x35DF50;  // (Queue*, void** item)
inline constexpr uint8_t kQueuePushHead[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
                                             0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xF9};
// The game's two allocators (docs/research/pendientes/p8-heap-inyeccion.md). Every one of the
// 22443 call sites in .rsrc picks between them with the same test, e.g. PostPacket 0x35C692..0x35C6BA:
// TLS block [gs:0x58][_tls_index] +0x1FD != 0 -> 0x47EFA78, else 0x47EF9B0. In both host dumps of
// 2026-10-09 no thread had +0x1FD set: the game only uses 0x47EF9B0.
//   0x47EFA78  a global mi_heap (created lazily by 0x47F19A8, mi_heap_new_in_arena: it shares the tld
//              of the thread that created it) behind the CRITICAL_SECTION exe+0x6C169C8. NEVER called
//              by the mod any more.
//   0x47EF9B0  the calling thread's own heap ([TLS+0x90], initialised on demand by 0x48953C4).
inline constexpr uintptr_t kAllocGlobal = 0x47EFA78;  // (size, align) -> void*; global heap with a lock
inline constexpr uint8_t kAllocGlobalHead[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20,
                                               0x48, 0x8B, 0xF9, 0x48, 0x8B, 0xDA};
inline constexpr uintptr_t kAllocThread = 0x47EF9B0;  // (size, align) -> void*; the thread's own heap
// The whole function (0x47EF9B0..0x47EFA0C, 92 bytes; unique in the file).
inline constexpr uint8_t kAllocThreadBytes[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x65, 0x48, 0x8B,
    0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xFA, 0x44, 0x8B, 0x05, 0x7E, 0x92, 0x49, 0x02, 0x48, 0x8B,
    0xF1, 0x4A, 0x8B, 0x1C, 0xC0, 0xB8, 0x98, 0x00, 0x00, 0x00, 0x80, 0x3C, 0x18, 0x00, 0x75, 0x05, 0xE8, 0xDB,
    0x59, 0x0A, 0x00, 0xB9, 0x90, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xC7, 0x48, 0x8B, 0xD6, 0x48, 0x8B, 0x0C, 0x19,
    0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x8B, 0x74, 0x24, 0x38, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xE9, 0x3C, 0xFC,
    0xFF, 0xFF};
inline constexpr uintptr_t kTlsIndexSlot = 0x6C88C50;  // u32 _tls_index (0 in the dumps)
inline constexpr uint32_t kTls_UseGlobalHeap = 0x1FD;  // u8: the allocator test above
// The server's thread park flags: ServerNet::RegisterThread 0x27751D0 (called first by ServerRunThread's
// loop 0x27733F0) appends {u32 tid, u8* &TLS[+0x4460]} to [ServerNet+0xF8], count [+0x100] (lock +0x98).
// The loops 0x27733F0 / 0x27731C0 spin Sleep(10) with byte[1] = 1 while byte[0] != 0, after taking an
// item (byte[2] = 1 while one is being handled). Read only by the mod (diagnostics).
inline constexpr uint32_t kServerNet_Threads = 0xF8;
inline constexpr uint32_t kServerNet_ThreadCount = 0x100;
inline constexpr uint32_t kServerThreadEntry = 0x10;

// Recipient finders: vf1 (finder*, Array<NwSession*>* out). The two
// "except one" ones are emptied in this build (segunda-sesion.md §2.3).
inline constexpr uintptr_t kToNearExceptOneControlVtable = 0x5B25358;
inline constexpr uintptr_t kToNearExceptOneSelfVtable = 0x5B25378;
inline constexpr uintptr_t kToNearVtable = 0x5B25418;
inline constexpr uintptr_t kExceptOneVf1 = 0x2A759B0;      // control
inline constexpr uintptr_t kExceptOneSelfVf1 = 0x2A75880;  // self
inline constexpr uintptr_t kToNearVf1 = 0x2A75860;
inline constexpr uint8_t kExceptOneVf1Head[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0x41, 0x10, 0x48,
                                                0x8B, 0x48, 0x68, 0x48, 0x8B, 0x99, 0xA0, 0x01, 0x00, 0x00};
inline constexpr uint8_t kToNearVf1Head[] = {0x48, 0x8B, 0x41, 0x10, 0x48, 0x8B, 0x48, 0x68,
                                             0x48, 0x8B, 0x89, 0xA0, 0x01, 0x00, 0x00, 0xE9};
inline constexpr uint32_t kFinder_Actor = 0x10;
// The game's array: T* data | u32 count | u32 capacity (0x2BC4F70).
inline constexpr uint32_t kArray_Count = 0x08;
inline constexpr uint32_t kArray_Capacity = 0x0C;

// C->S packet buffer (ClientNet::PostPacket, 0x8030 bytes; D0 §4).
inline constexpr uint32_t kPbSize = 0x8030;
inline constexpr uint32_t kPb_FramePtr = 0x8000;  // u8*: the frame (= pb)
inline constexpr uint32_t kPb_Len1 = 0x8008;      // u16 frame length
inline constexpr uint32_t kPb_Len2 = 0x800A;      // u16 frame length (the server reads this one)
inline constexpr uint32_t kPb_Cap = 0x800C;       // u16
inline constexpr uint32_t kPb_IsRequest = 0x800E; // u8: 0 = packet
inline constexpr uint32_t kPb_Tag = 0x8010;       // u32, not read for packets: our tag
inline constexpr uint32_t kPb_Magic = 0x8018;     // u64, not read by the server: our magic
inline constexpr uint32_t kPb_Base = 0x8028;      // void*: what the client frees
inline constexpr uint32_t kFrameOverhead = 0x1F;  // frame = u16 len | u8 channel | packet | 12 IV + 16 tag
inline constexpr uint32_t kFrameHeader = 3;
// S->C: the frame starts at AsyncRequestSend+0x30.
inline constexpr uint32_t kRq_Frame = 0x30;

// Troc handler instances (the sniffer's table): u32 kind +0x08, u16 opcode
// +0x0C, u8 channel +0x14, u16 flags +0x16.
inline constexpr uintptr_t kTrocInstancesBegin = 0x6A51AE0;
inline constexpr uintptr_t kTrocInstancesEnd = 0x6A82C80;
inline constexpr uint32_t kTroc_Kind = 0x08;
inline constexpr uint32_t kTroc_Opcode = 0x0C;
inline constexpr uint32_t kTroc_Channel = 0x14;
inline constexpr uint32_t kTroc_Flags = 0x16;
inline constexpr uint32_t kTrocKindServer = 4;

// P8, the guest's thin client (docs/research/opcion-d-p8.md §7).
inline constexpr uint32_t kClientNet_Session = 0x10;   // NwVirtualSyncSession (persistent)
inline constexpr uint32_t kClientNet_FreeC2S = 0x28;   // Q3: C->S buffers the NetworkThread frees
inline constexpr uint32_t kClientNet_Slot4 = 0x58;     // != 0 once the client connected (1001)
inline constexpr uint32_t kSyncSession_Epoch = 0xB0;   // new on every connection (OnConnect 0x8CAFF0)
inline constexpr uintptr_t kSyncSessionSend = 0x14380B0;  // vf6 (this, int* res, PacketBuffer* pb) -> res
inline constexpr uint8_t kSyncSessionSendHead[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x89, 0xE0,
                                                   0x00, 0x00, 0x00, 0x48, 0x8B, 0xDA};
inline constexpr uintptr_t kNetLoopPump = 0x35BAD4;  // NetworkThread loop, each turn (<= 16 ms); r15 = ClientNet
inline constexpr uint8_t kNetLoopPumpHead[] = {0x49, 0x8B, 0x87, 0xE0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x38,
                                               0x48, 0x89, 0x7D, 0xC7};
// The host's own NetworkThread, same point (p8-heap-inyeccion.md): the native forwarder of C->S
// buffers into Q0 (FlushSendQueue 0x35BB05 -> 0x14380B0 -> 0x35DF50) and their freer (Q3). The
// host's server.cpp hooks it to hand the guest's Req; the guest's guest.cpp hooks it on the other
// PC (the two roles never run in one process, mod.cpp). 39 bytes, unique in the file; the only
// jump into them lands on their first byte (0x35BAC6).
// The bytes safetyhook's 5-byte jmp relocates there: exactly the first instruction, mov rax,[r15+0xE0]
// (7 bytes). p8_install_mid refuses any other size (e.g. the 14-byte fallback) (P8 review M3).
inline constexpr uint32_t kNetLoopPumpPatch = 7;
inline constexpr uint8_t kNetLoopPumpAob[] = {
    0x49, 0x8B, 0x87, 0xE0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x38, 0x48, 0x89, 0x7D, 0xC7, 0x48, 0x8B, 0x07, 0x48, 0x8B, 0xCF,
    0xFF, 0x50, 0x08, 0x90, 0x8B, 0x5F, 0x58, 0x48, 0x8B, 0x07, 0x48, 0x8B, 0xCF, 0xFF, 0x50, 0x10, 0x85, 0xDB, 0x74};
inline constexpr uintptr_t kClientOnRecvPacket = 0x35CA00;  // (ClientNet*, NwSession*, -, u8* pkt, u16 size)
inline constexpr uint8_t kClientOnRecvPacketHead[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89,
                                                      0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24};

// P8 title (docs/research/pendientes/p8-titulo.md): the save manager's slot
// table, which the title reads for Play/Load and only the server's
// "refresh slots from disk" 0x1870C20 rebuilds. Checked byte for byte before
// the guest calls it (guest.cpp, refresh_title_slots).
inline constexpr uintptr_t kWorldSystemSlot = 0x6D69190;      // [exe+..] = WorldSystem
inline constexpr uint32_t kWorldSystem_SaveManager = 0x88;     // the save manager the title reads
inline constexpr uintptr_t kServerActorLookupSlot = 0x6D69698;  // [exe+..]; +0x48 = the server's save manager
inline constexpr uint32_t kServerActorLookup_SaveManager = 0x48;
// The save manager (ctor .xtls 0xCBD2920, one construction site 0xACDA95).
inline constexpr uint32_t kSaveMgr_Lock = 0x00;     // pa::WindowsLock<3>* (= this + 8)
inline constexpr uint32_t kSaveMgr_LockObj = 0x08;  // the lock itself (CRITICAL_SECTION inside)
inline constexpr uint32_t kSaveMgr_Slots = 0x40;    // entries of 0x68 bytes: u32 slot @0, i64 time @8
inline constexpr uint32_t kSaveMgr_Count = 0x48;    // u32
inline constexpr uint32_t kSaveMgr_Capacity = 0x4C; // u32
inline constexpr uint32_t kSaveMgr_Storage = 0x50;  // SaveStorage*; +0x38 slot folders, +0x40 their count
inline constexpr uint32_t kSaveMgr_Busy = 0x60;     // u8: a refresh/login holds the table
inline constexpr uint32_t kSaveMgr_EntrySize = 0x68;
inline constexpr uint32_t kSaveStorage_FolderCount = 0x40;
inline constexpr uintptr_t kWindowsLock3Vtable = 0x557C120;  // vf1 -> EnterCriticalSection, vf2 -> Leave
inline constexpr uintptr_t kWindowsLockVf1 = 0x1371340;      // mov rax,[rcx]; jmp [rax+0x18]
inline constexpr uintptr_t kWindowsLockVf2 = 0xCE4EF0;       // mov rax,[rcx]; jmp [rax+0x20]
inline constexpr uint8_t kWindowsLockVf1Head[] = {0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, 0x18};
inline constexpr uint8_t kWindowsLockVf2Head[] = {0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, 0x20};
// 0x1870C20: jmp .xtls 0xCBD92D0 (refresh the slot table from the slot folders).
inline constexpr uintptr_t kSaveMgrRefreshSlots = 0x1870C20;
inline constexpr uint8_t kSaveMgrRefreshSlotsHead[] = {0xE9, 0xAB, 0x86, 0x36, 0x0B};
// SaveDataSlotRefreshReq 0x2A22380, +0xF5..+0x172: the native sequence the
// guest copies (gestor = [[0x6D69698]+0x48]; lock vf1; test/set +0x60;
// vf2; call 0x1870C20; lock vf1; +0x60 = 0; vf2), without its Ack 0x2A206E0.
inline constexpr uintptr_t kSlotRefreshReqSequence = 0x2A22475;
inline constexpr uint8_t kSlotRefreshReqSequenceBytes[] = {
    0x48, 0x8B, 0x05, 0x1C, 0x72, 0x34, 0x04,  // mov rax,[exe+0x6D69698]
    0x4C, 0x8B, 0x78, 0x48,                    // mov r15,[rax+0x48]
    0x4C, 0x89, 0x7D, 0x07, 0x45, 0x32, 0xF6, 0x44, 0x88, 0x75, 0x0F,
    0x49, 0x8B, 0x1F,                          // mov rbx,[r15]          (the lock)
    0x48, 0x89, 0x5D, 0x77, 0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB,
    0xFF, 0x50, 0x08, 0x90,                    // call [rax+8]           (lock)
    0x45, 0x38, 0x77, 0x60, 0x75, 0x0C,        // cmp [r15+0x60],r14b; jne busy
    0x41, 0xC6, 0x47, 0x60, 0x01,              // mov byte [r15+0x60],1
    0x41, 0xB6, 0x01, 0x44, 0x88, 0x75, 0x0F, 0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB,
    0xFF, 0x50, 0x10, 0x90,                    // call [rax+0x10]        (unlock)
    0x45, 0x84, 0xF6, 0x75, 0x05, 0xE9, 0x91, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x0D, 0xCF, 0x71, 0x34, 0x04,  // mov rcx,[exe+0x6D69698]
    0x48, 0x8B, 0x49, 0x48,                    // mov rcx,[rcx+0x48]
    0xE8, 0x4E, 0xE7, 0xE4, 0xFE, 0x90,        // call 0x1870C20
    0x49, 0x8B, 0x1F, 0x48, 0x89, 0x5D, 0x77, 0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB,
    0xFF, 0x50, 0x08, 0x90,                    // call [rax+8]           (lock)
    0x41, 0xC6, 0x47, 0x60, 0x00,              // mov byte [r15+0x60],0
    0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x50, 0x10};  // call [rax+0x10] (unlock)
// The title's own read of the table (Play, 0x65DB90): mov r11,[exe+0x6D69190]; mov rax,[r11+0x88].
inline constexpr uintptr_t kTitlePlayReadsSaveMgr = 0x65DC3E;
inline constexpr uint8_t kTitlePlayReadsSaveMgrBytes[] = {0x4C, 0x8B, 0x1D, 0x4B, 0xB5, 0x70, 0x06,
                                                          0x49, 0x8B, 0x83, 0x88, 0x00, 0x00, 0x00};

inline constexpr uintptr_t kImageBase = 0x140000000;
inline constexpr uint32_t kPeTimestamp = 0x6AB28F00;

}  // namespace cdmp::session2::off
