"""P8 option U, step U2 case A (docs/research/s25/tiendas-u2.md): a cdmp_peer script in which user 2 logs in with a
walk script's login, walks it, then walks on to the Hernand SMITHY's counter, waits there until its OWN manager (U-1
armed, user2_stages_elements=schedule+funcnpc) prepares the cd_seq_funcnpc_smithy scene for it (learn_2661: the
2661 whose owner is the peer's character and whose resource hash is the smithy's gives SEQ and the smithy NPC), and
then plays the native shop talk of that resource as the host's client played it in session 13 (capture
p8-sesion13-host\\cdmp_packets_20261011-003030.bin, t = 541.37 .. 552.77, sequencer 0x19D = cd_seq_funcnpc_smithy,
NPC B0100159, interaction 0x000F4321):

    2671 (requester, NPC, 0x000F4321) -> 2720 -> 2436 SEQ branch 6 + four 2173 -> 2436 branch 0 + four 2173
    -> 2436 branch 3 + four 2173 -> 2436 branch 1 + four 2173

(no 2591 of the shop's own sequencer: the native client sent none for it, S12's butcher neither; its 2767 trigger
events name other NPCs and are left out). Then it stands still sending one MoveActorReq a second (U-1's requests run
at the exit of user 2's Reqs) and says its native goodbye.

    py -I -B tools\\scripts\\make_shop_script.py docs\\research\\data\\grapple_s25_g1_walk_138_217.troc ^
        build\\e2e\\u2_shop.troc --wait-s 180 --after-s 150

--no-talk: the walk and the standing only (step A: no learn_2661, no shop talk, nothing can fail).
--no-expect: the shop talk without waiting for 2663 / 2916 (step A2, the probe: the peer's 2671 reaches the host's
manager and gets a 3000, so `expect 2663` would stop the script); by default (step B) each one is expected (5 s).
The NPC is the smithy's own link (node 0x20000008 in the S13 2661: learn_2661's NODE), never just any NPC of the scene.
The walk is a straight line on the ground plane with the height interpolated linearly between the two ends (the
server is not known to check either; a refused walk shows as the peer's position not changing in the host's log).
The output is a local file (not checked in, not listed in check_troc_scripts.py). Game build 1.0.0.2976: the
opcodes, the payloads and the positions are this build's (MoveActorReq 2556: u32 actor @0, position 3 floats @4 and
@76, rotation quaternion @16 and @88, velocity 3 floats @116; the yaw convention measured on the walk script:
yaw = atan2(dx, dz) + 180 deg).
"""
import argparse
import math
import struct
import sys

# The smithy (S13, the host's client): its resource hash (2661 @27), where the host stood when it talked (its last
# MoveActorReq before the 2671) and its rotation there.
SMITHY_HASH = 0xA7132561
SMITHY_NODE = 0x20000008  # the S13 2661's bindings: 0x20000002 -> the Kliff, 0x20000008 -> the smithy B0100159
STAND = (-10637.741, 613.376, -3850.726)
STAND_Q = (0.0, -0.9592442, 0.0, 0.2825787)
SHOP_INTERACTION = 0x000F4321
# The 2173s after each branch (u32 key @8, u8 value @12, then 00 0F), in the native order.
GAMEPLAY = {
    6: [(0x60000000, 0), (0x60000002, 0), (0x60000003, 1), (0x60000005, 1)],
    0: [(0x60000003, 0), (0x60000005, 0), (0x60000006, 1), (0x60000008, 1)],
    3: [(0x60000006, 0), (0x60000008, 0), (0x60000015, 1), (0x60000017, 1)],
    1: [(0x60000015, 0), (0x60000017, 0), (0x60000000, 1), (0x60000002, 1)],
}
# Branch, then the wait after its 2173s (ms), as in S13 (541.43 -> 542.44 -> 549.74 -> 552.75).
BRANCHES = [(6, 1000), (0, 7300), (3, 3000), (1, 2000)]


def move_req(template: bytes, pos, quat, vel) -> str:
    b = bytearray(template)
    for off in (4, 76):
        struct.pack_into("<3f", b, off, *pos)
    for off in (16, 88):
        struct.pack_into("<4f", b, off, *quat)
    struct.pack_into("<3f", b, 116, *vel)
    return b.hex()


def yaw_quat(dx: float, dz: float):
    yaw = math.atan2(dx, dz) + math.pi
    return (0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("walk", help="a walk script that ends with: wait / logout_session / expect 2003 / expect 2002")
    ap.add_argument("out")
    ap.add_argument("--wait-s", type=int, default=180, help="standing at the counter before SEQ must be known")
    ap.add_argument("--after-s", type=int, default=150, help="standing still after the shop talk")
    ap.add_argument("--step-m", type=float, default=0.6)
    ap.add_argument("--every-ms", type=int, default=100)
    ap.add_argument("--no-talk", action="store_true",
                    help="walk and stand only (no learn_2661, no expect_learned, no shop talk): step A")
    ap.add_argument("--no-expect", action="store_true",
                    help="do not expect 2663 / 2916 after the 2671 / 2720 (step A2, the probe)")
    a = ap.parse_args()
    with open(a.walk, encoding="utf-8") as f:
        lines = f.read().replace("\r\n", "\n").split("\n")
    try:
        bye = max(i for i, s in enumerate(lines) if s.strip() == "logout_session")
    except ValueError:
        sys.exit("no logout_session in the walk script")
    head_end = bye
    if head_end > 0 and lines[head_end - 1].startswith("wait "):
        head_end -= 1
    head = lines[:head_end]
    tail = [s for s in lines[bye:] if s.strip()]
    if not any(s.startswith("learn PLAYER_KEY ") for s in head):
        sys.exit("the walk script does not learn PLAYER_KEY")
    last = max((i for i, s in enumerate(head) if s.startswith("send MoveActorReq ")), default=-1)
    if last < 0:
        sys.exit("no MoveActorReq in the walk script")
    tpl = bytes.fromhex(head[last].split("  #")[0].split()[4])
    if len(tpl) < 128:
        sys.exit("the MoveActorReq is too short")
    start = struct.unpack_from("<3f", tpl, 4)

    def send_move(pos, quat, vel):
        return [f"send MoveActorReq 2556 0 {move_req(tpl, pos, quat, vel)}", "put u32 0 PLAYER_KEY"]

    out = [
        "# P8 option U, U2 (docs/research/s25/tiendas-u2.md): user 2 walks to the Hernand smithy, waits for ITS OWN",
        "# cd_seq_funcnpc_smithy scene (learn_2661) and plays the native shop talk of session 13 on it.",
        "# Generated by tools/scripts/make_shop_script.py; not checked in.",
    ] + head
    if not a.no_talk:
        out += [
            "",
            "# U2: its own smithy scene (owner = PLAYER_KEY, resource hash of cd_seq_funcnpc_smithy).",
            f"var SHOP_HASH u32 0x{SMITHY_HASH:08X}",
            f"var SHOP_NODE u32 0x{SMITHY_NODE:08X}",
            "learn_2661 SEQ NPC SHOP_HASH PLAYER_KEY SHOP_NODE",
        ]
    dx, dz = STAND[0] - start[0], STAND[2] - start[2]
    dist = math.hypot(dx, dz)
    steps = max(1, int(dist / a.step_m))
    q = yaw_quat(dx, dz)
    speed = a.step_m / (a.every_ms / 1000.0)
    vel = (dx / dist * speed, 0.0, dz / dist * speed) if dist > 0 else (0.0, 0.0, 0.0)
    out += ["", f"# Walk {dist:.0f} m to the smithy's counter ({steps} steps of {a.step_m} m every {a.every_ms} ms)."]
    for i in range(1, steps + 1):
        t = i / steps
        pos = (start[0] + dx * t, start[1] + (STAND[1] - start[1]) * t, start[2] + dz * t)
        out.append(f"wait {a.every_ms}")
        out += send_move(pos, q, vel)
    out += ["", f"# Stand at the counter for {a.wait_s} s (U-1 arms, its manager evaluates this cell: the 2661)."]
    for _ in range(a.wait_s):
        out.append("wait 1000")
        out += send_move(STAND, STAND_Q, (0.0, 0.0, 0.0))
    talk = [] if a.no_talk else BRANCHES
    if not a.no_talk:
        out += ["expect_learned SEQ 1000", "expect_learned NPC 1000", "",
                "# The native shop talk (S13 smithy): 2671 -> 2720 -> the scene's branches and game-play events."]
        pre = struct.pack("<IIIIIIB", 0, 0, SHOP_INTERACTION, 0, 0, 1, 0)
        out += [f"send InteractionPreemptionStepReq 2671 0 {pre.hex()}", "put u32 0 PLAYER_KEY", "put u32 4 NPC",
                "wait 30" if a.no_expect else "expect 2663 5000  # InteractionPreemptionStepSuccessAck (a 3000 stops here)"]
        do = struct.pack("<IIII", 0, 0, SHOP_INTERACTION, 0)
        out += [f"send InteractionDoStepReq 2720 0 {do.hex()}", "put u32 0 PLAYER_KEY", "put u32 4 NPC",
                "wait 30" if a.no_expect else "expect 2916 5000  # InteractionDoStepSuccessAck"]
    for branch, after in talk:
        out += [f"send StageBranchStateReq 2436 0 {struct.pack('<QIBB', 0, branch, 0, 0).hex()}", "put u64 0 SEQ",
                "wait 25"]
        for key, val in GAMEPLAY[branch]:
            out += [f"send UpdateSequencerGamePlayDataEventReq 2173 0 {struct.pack('<QIBBB', 0, key, val, 0, 0x0F).hex()}",
                    "put u64 0 SEQ"]
        out.append(f"wait {after}")
    out += ["", f"# Standing still {a.after_s} s (U-1's requests, the drain wait if the host comes back)."]
    for _ in range(a.after_s):
        out.append("wait 1000")
        out += send_move(STAND, STAND_Q, (0.0, 0.0, 0.0))
    out += ["wait 2000"] + tail
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print(f"{a.out}: {len(head)} lines of the walk, a {dist:.0f} m walk in {steps} steps, {a.wait_s} s at the counter, "
          f"{'no shop talk' if a.no_talk else 'the shop talk'}, {a.after_s} s standing, then the goodbye")


if __name__ == "__main__":
    main()
