#pragma once
// P8 S1 (docs/research/pendientes/p8-s1-escenas-npc.md, "Implementado para la
// medida"): the G1 points, shared by the host's (H2) and the guest's (G1)
// probes: the NPC's AI block read from its 2005. Bytes read from the
// authorised read-only PE (D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe,
// build 1.0.0.2976) with capstone over the whole function (.pdata chunks
// merged): whole instructions, no branch of the function and no real rel32
// into the patch, no RIP-relative operand, the registers read not written
// between their definition and the point.

#include <cstdint>

namespace cdmp::session2::scp {

// ClientAiActorComponent::vf11 0x206DBD0 (rdi = the AI component from 0x206DBE2, never written again
// before the point) calls the AI block reader 0x1EB88C0 (its only caller, 0x206DE66) with rcx = X1 =
// [ai+0x90]. The reader: u8 -> X1+0x61, u32 resource -> [rbp+0x50], u32 node -> [rbp+0x54],
// 0x1869D70(&result, (node << 32) | resource) -> [X1+8] = handle {u64, u32 index, file}, u64 -> X1+0.

// 0x1EB8946 mov rdx,[r14+8] ; mov rcx,[rax]   (RAX -> 0x1869D70's result, after its call and nop)   patch 7
inline constexpr uintptr_t kAiRead = 0x1EB8946;
inline constexpr uint32_t kAiReadPatch = 7;
inline constexpr uint8_t kAiReadBytes[] = {0x49, 0x8B, 0x56, 0x08, 0x48, 0x8B, 0x08, 0x48,
                                           0x89, 0x0A, 0x8B, 0x48, 0x08, 0x89, 0x4A, 0x08};
// 0x1EB88CB mov rbp,rsp
inline constexpr uintptr_t kAiReadFrame = 0x1EB88CB;
inline constexpr uint8_t kAiReadFrameBytes[] = {0x48, 0x8B, 0xEC};
// 0x1EB88D5 mov r14,rcx (X1)
inline constexpr uintptr_t kAiReadArg = 0x1EB88D5;
inline constexpr uint8_t kAiReadArgBytes[] = {0x4C, 0x8B, 0xF1};
// 0x1EB88F8 mov [rbp+0x50],esi ; mov dword [rbp+0x54],-1
inline constexpr uintptr_t kAiReadInit = 0x1EB88F8;
inline constexpr uint8_t kAiReadInitBytes[] = {0x89, 0x75, 0x50, 0xC7, 0x45, 0x54, 0xFF, 0xFF, 0xFF, 0xFF};
// 0x1EB8938 mov rdx,[rbp+0x50] ; lea rcx,[rbp-0x20] ; call 0x1869D70 ; nop
inline constexpr uintptr_t kAiReadCall = 0x1EB8938;
inline constexpr uint8_t kAiReadCallBytes[] = {0x48, 0x8B, 0x55, 0x50, 0x48, 0x8D, 0x4D,
                                               0xE0, 0xE8, 0x2B, 0x14, 0x9B, 0xFF, 0x90};

// 0x206DE6B test al,al ; je 0x206DF3E (rel32, relocated)                                         patch 8
inline constexpr uintptr_t kAiDone = 0x206DE6B;
inline constexpr uint32_t kAiDonePatch = 8;
inline constexpr uint8_t kAiDoneBytes[] = {0x84, 0xC0, 0x0F, 0x84, 0xCB, 0x00, 0x00, 0x00,
                                           0x48, 0x8B, 0x97, 0x98, 0x00, 0x00, 0x00, 0x48};
// 0x206DBE2 mov rdi,rcx (the AI component)
inline constexpr uintptr_t kAiDoneThis = 0x206DBE2;
inline constexpr uint8_t kAiDoneThisBytes[] = {0x48, 0x8B, 0xF9};
// 0x206DE5C mov rcx,[rdi+0x90] ; mov rdx,rbx ; call 0x1EB88C0
inline constexpr uintptr_t kAiDoneCall = 0x206DE5C;
inline constexpr uint8_t kAiDoneCallBytes[] = {0x48, 0x8B, 0x8F, 0x90, 0x00, 0x00, 0x00, 0x48,
                                               0x8B, 0xD3, 0xE8, 0x55, 0xAA, 0xE4, 0xFF};

// Structures the G1 callbacks read.
inline constexpr uint32_t kComp_Actor = 0x08;   // an actor component's actor
inline constexpr uint32_t kAi_Scene = 0x90;     // X1
inline constexpr uint32_t kScene_Handle = 0x08, kScene_Flag = 0x61;
inline constexpr uint32_t kHandle_Index = 0x08, kHandle_File = 0x10;
inline constexpr uint32_t kActor_Key = 0x60, kActor_Comps = 0x68, kComps_PlayData = 0xB0;
// ClientSequencerPlayDataActorComponent's vtable (slot 13 = vf13 0x9ED290); [[A+0x68]+0xB0] is that
// component (0x91CDB8, 0xC0D20F); its +0x2D8 = the sequencer owner actor, +0x2E0 = the scene key.
inline constexpr uintptr_t kVtPlayData = 0x55BEE78;
inline constexpr uint32_t kPlayData_Owner = 0x2D8, kPlayData_Key = 0x2E0;

}  // namespace cdmp::session2::scp
