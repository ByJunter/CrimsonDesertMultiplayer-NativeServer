"""Cuts one or more grappling-hook (RemoteCatch) episodes of a player out of a sniffer capture (.bin, CDPK) and
appends them, as `send` lines with the character's key replaced by PLAYER_KEY, to an option D login script
(src/common/troc_script.h), so cdmp_peer --mode replay plays that player's rope as user 2 on ONE PC
(docs/research/s25/gancho-cuerda.md §8 and "Implementado").

    py -3 -I tools/scripts/make_grapple_script.py CAPTURE KEYHEX T0 T1 LOGIN.troc OUT.troc
        [--scene HEX32[,HEX32...]] [--no-moves] [--no-rotations] [--move-hz HZ] [--bye MS]

Kept, client->server only, at the original pace (`wait` = the gaps):
  StartActionReq 2223 and ProcessAfterBeginFrameEventReq 2189 of the character (key @0),
  DoFrameEventReq 2380 of the character (attacker @1; the 636-byte variant 6 carries the target: the tree's scene id
  and the THROWER's physics-body handle, left as captured: that handle is what the host's client resolves wrongly),
  MoveActorReq 2556 / RotateActorReq 2828 of the character (key @0; --no-moves / --no-rotations leave them out),
  IsRemoteCatchEffectStartStateReq 2108,
  and, sent once before the episodes, the last SceneLevelOnDemandGimmickSpawnReq 2928 of each --scene (the tree's
  16-byte scene id) before T0 (else its first one).
--move-hz keeps at most HZ MoveActorReq (and as many RotateActorReq) per second (the client sends one per frame).
Everything else (other actors, the tool's gimmick traffic) is left out. Game build 1.0.0.2976.

Every script also answers each ChangeActionSessionIDAck 2954 (a server-initiated action of user 2's character) with
EchoActionSessionIDReq 2350 {u32 actor, u64 session}, as the real client does: until it does, the server accepts
the character's action Reqs WITHOUT applying them (opcion-d-accion-usuario2.md §2.1 #11), so no frame event of the
rope runs and no RemoteCatchResultAck 2763 comes (the one-PC run of 2026-10-12 19:04: a 2954 right after the first
replayed StartActionReq, then no 2763).

S25 (G1 = Shadow's cdmp_packets_20261012-054022.bin, character 0xA010034E):
  ... A010034E 136.0 172.5 docs/research/data/login_e3lc00_112627.troc docs/research/data/grapple_s25_g1_138.troc \
      --scene 82fc9066de0000090ffe000100000000 --bye 10000
  ... A010034E 136.0 225.0 ... grapple_s25_g1_138_217.troc \
      --scene 82fc9066de0000090ffe000100000000,d93a552cda0000090ffe000100000000 --no-moves --no-rotations --bye 10000
  ... A010034E 979.0 986.0 ... grapple_s25_g1_981.troc --scene 83f89066de0000090ffe000100000000 --bye 10000
  ... A010034E 105.0 225.0 ... grapple_s25_g1_walk_138_217.troc       --scene 82fc9066de0000090ffe000100000000,d93a552cda0000090ffe000100000000 --move-hz 10 --bye 10000
      (the walk: in S25 the guest started where slot 108 starts, (-10717.7, 620.2, -3914.6), and walked to the tree
      spot (-10748.8, 615.7, -3939.5) between 111 and 127 s; user 2 logged in with SAVE_SLOT=108 walks the same way)
"""
import csv
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CSV = os.path.join(HERE, "..", "..", "docs", "research", "data", "troc_messages_2976.csv")
REC = struct.Struct("<QBBHHHII")  # capture_file.h BinRecord: t_us, dir, flags, size, copied, pad, session, thread


def names():
    out = {}
    with open(CSV, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            out[int(row["id"])] = row["name"]
    return out


def records(path):
    """(t seconds since the first record, dir, opcode, slot, payload) of every whole record."""
    with open(path, "rb", buffering=1 << 24) as f:
        if f.read(12)[:4] != b"CDPK":
            raise SystemExit(f"{path}: not a CDPK capture")
        first = None
        while True:
            h = f.read(REC.size)
            if len(h) < REC.size:
                return
            t, d, _fl, size, copied, _pad, _sess, _thr = REC.unpack(h)
            pkt = f.read(copied)
            if first is None:
                first = t
            if len(pkt) < 5 or copied != size:
                continue
            op = pkt[0] | pkt[1] << 8
            slot = pkt[2] if pkt[2] < 128 else pkt[2] - 256
            yield (t - first) / 1e6, d, op, slot, pkt[5:]


def main():
    pos, opts = [], {}
    a = sys.argv[1:]
    i = 0
    while i < len(a):
        if a[i] in ("--scene", "--bye", "--move-hz"):
            opts[a[i]] = a[i + 1]
            i += 2
        elif a[i].startswith("--"):
            opts[a[i]] = True
            i += 1
        else:
            pos.append(a[i])
            i += 1
    if len(pos) != 6:
        raise SystemExit(__doc__)
    cap, keyhex, t0, t1, login, out = pos
    key = int(keyhex, 16)
    t0, t1 = float(t0), float(t1)
    kb = key.to_bytes(4, "little")
    scenes = [bytes.fromhex(x) for x in opts["--scene"].split(",")] if "--scene" in opts else []
    nm = names()
    keep, spawns = [], {}
    hz = float(opts.get("--move-hz", 0))
    last_kept = {}

    def thin(op, t):
        if hz <= 0:
            return True
        if t - last_kept.get(op, -1e9) >= 1.0 / hz - 1e-6:
            last_kept[op] = t
            return True
        return False
    for t, d, op, slot, p in records(cap):
        if t > t1:
            break
        if d != 1:
            continue
        hit = [s for s in scenes if s in p] if op == 2928 else []
        if hit:
            if t < t0 or hit[0] not in spawns:
                spawns[hit[0]] = (t, slot, p)
            continue
        if t < t0:
            continue
        if op in (2223, 2189) and p[:4] == kb:
            keep.append((t, op, slot, p, 0))
        elif op == 2556 and p[:4] == kb and not opts.get("--no-moves"):
            if thin(op, t):
                keep.append((t, op, slot, p, 0))
        elif op == 2828 and p[:4] == kb and not opts.get("--no-rotations"):
            if thin(op, t):
                keep.append((t, op, slot, p, 0))
        elif op == 2380 and p[1:5] == kb:
            keep.append((t, op, slot, p, 1))
        elif op == 2108:
            keep.append((t, op, slot, p, None))
    with open(login, encoding="utf-8") as f:
        lines = f.read().rstrip("\n").split("\n")
    o = [f"# P8 S25 grappling hook (RemoteCatch) of a player, cut by tools/scripts/make_grapple_script.py from "
         f"{os.path.basename(cap)}",
         f"# key 0x{key:08X} -> PLAYER_KEY, t {t0:.3f}..{t1:.3f} s, {len(keep)} Req"
         f"{'' if not opts.get('--no-moves') else ', no MoveActorReq'}"
         f"{'' if not opts.get('--no-rotations') else ', no RotateActorReq'}"
         f"{'' if not hz else f', moves / rotations at most {hz:g} per second'}; the login below is "
         f"{os.path.basename(login)}.",
         "# The DoFrameEventReq variant 6 (636 B) keeps the scene id and the THROWER's own physics-body handle as captured",
         "# (docs/research/s25/gancho-cuerda.md). Game build 1.0.0.2976."]
    o += lines
    o += ["", "# ---- grapple episode(s) ----",
          "# As the real client: every ChangeActionSessionIDAck 2954 gets its EchoActionSessionIDReq 2350",
          "# {u32 actor, u64 session} (the 2954 has {u32 actor, u32 user, u64 session}); without it the server takes the action",
          "# Reqs without applying them.",
          "learn AS_KEY u32 2954 0",
          "learn AS_ID u64 2954 8",
          "reply 2954 2350 0 000000000000000000000000",
          "put u32 0 AS_KEY",
          "put u64 4 AS_ID",
          "wait 5000  # settle after the load"]
    for sc, (t, slot, p) in spawns.items():
        o.append(f"# tree on-demand gimmick {sc.hex()} (as the player's client asked, t {t:.3f})")
        o.append(f"send SceneLevelOnDemandGimmickSpawnReq 2928 {slot} {p.hex()}")
        o.append("wait 500")
    last = None
    for t, op, slot, p, off in keep:
        if last is not None:
            gap = int(round((t - last) * 1000))
            if gap > 0:
                o.append(f"wait {gap}")
        last = t
        o.append(f"send {nm.get(op, 'Op' + str(op))} {op} {slot} {p.hex() if p else '-'}  # t {t:.3f}")
        if off is not None:
            o.append(f"put u32 {off} PLAYER_KEY")
    if "--bye" in opts:
        o += [f"wait {int(opts['--bye'])}", "logout_session", "expect 2003 30000  # LogoutUserToServerAck",
              "expect 2002 30000  # LogoutSessionToServerAck"]
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(o) + "\n")
    counts = {}
    for _, op, *_ in keep:
        counts[nm.get(op, op)] = counts.get(nm.get(op, op), 0) + 1
    print(out, counts, f"{len(spawns)} tree spawn(s)")


if __name__ == "__main__":
    main()
