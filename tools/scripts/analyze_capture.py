"""Analyze a cdmp sniffer capture (cdmp_packets_*.bin).

Usage:
  python analyze_capture.py <capture.bin> summary            # totals + timeline
  python analyze_capture.py <capture.bin> show <name|op> [N]   # first N packets of a type (hex)
  python analyze_capture.py <capture.bin> floats <name|op>     # float32 view of payloads
  python analyze_capture.py <capture.bin> phases               # activity per 10 s window
  python analyze_capture.py <capture.bin> trips                # P8 S2: each loading cycle (2174 ... 2762)
  python analyze_capture.py <capture.bin> acks                 # P8 S3: per-NPC packets before the key's 2005
  python analyze_capture.py <capture.bin> scenes [actor_hex]   # P8: each sequencer's 2661 ... 2810 (closed or not)
  python analyze_capture.py <capture.bin> ticks                # P8 pause: the server's game clock per ServerTimeInfoAck
  python analyze_capture.py <capture.bin> orphans              # P8 S15: child records (actor +0xAC) before their parent

Message names come from docs/research/data/troc_messages_<build>.csv.
Packet: u16 opcode | u8 slot | u16 payload_len | payload.
"""
import csv
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REC = struct.Struct("<QBBHHHII")  # t_us dir pad size copied pad session thread


def load_names():
    names = {}
    for p in sorted((ROOT / "docs/research/data").glob("troc_messages_*.csv")):
        with open(p, newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                names[int(row["id"])] = row["name"]
    return names


def read_capture(path):
    data = Path(path).read_bytes()
    if data[:4] != b"CDPK":
        raise SystemExit("not a cdmp capture")
    pos = 12
    out = []
    while pos + REC.size <= len(data):
        t, d, _, size, copied, _, sess, thread = REC.unpack_from(data, pos)
        pos += REC.size
        pkt = data[pos:pos + copied]
        pos += copied
        if len(pkt) < 5:
            continue
        op, slot, plen = struct.unpack_from("<HbH", pkt, 0)
        out.append((t, d, op, slot, plen, size, pkt[5:], thread))
    return out


def resolve(arg, names):
    if arg.isdigit():
        return int(arg)
    for op, n in names.items():
        if n.lower() == arg.lower():
            return op
    raise SystemExit(f"unknown message {arg}")


def summary(pkts, names):
    t0, t1 = pkts[0][0], pkts[-1][0]
    print(f"{len(pkts)} packets over {(t1 - t0) / 1e6:.0f} s")
    for d, label in ((1, "C>S"), (2, "S>C")):
        c = Counter(p[2] for p in pkts if p[1] == d)
        print(f"\n== {label}: {sum(c.values())} packets, {len(c)} message types")
        for op, n in c.most_common():
            sizes = [p[4] for p in pkts if p[1] == d and p[2] == op]
            print(f"  {names.get(op, '?'):45} {op:5} x{n:<7} len {min(sizes)}..{max(sizes)}")


def phases(pkts, names):
    t0 = pkts[0][0]
    windows = defaultdict(lambda: (Counter(), Counter()))
    for p in pkts:
        w = int((p[0] - t0) / 10e6)
        windows[w][0 if p[1] == 1 else 1][p[2]] += 1
    seen = set()
    for w in sorted(windows):
        c2s, s2c = windows[w]
        new = [names.get(op, str(op)) for op in list(c2s) + list(s2c) if op not in seen]
        seen.update(c2s)
        seen.update(s2c)
        top = ", ".join(f"{names.get(o, o)} x{n}" for o, n in c2s.most_common(4))
        print(f"{w * 10:5}s C>S {sum(c2s.values()):5} S>C {sum(s2c.values()):6} | {top}")
        if new:
            print(f"        new: {', '.join(new[:12])}{' …' if len(new) > 12 else ''}")


def show(pkts, names, op, n):
    rows = [p for p in pkts if p[2] == op][:n]
    t0 = pkts[0][0]
    for t, d, _, slot, plen, size, payload, thread in rows:
        print(f"{(t - t0) / 1e6:9.3f} {'C>S' if d == 1 else 'S>C'} slot {slot} len {plen} | "
              + payload[:96].hex(" "))


def floats(pkts, names, op):
    rows = [p for p in pkts if p[2] == op][:20]
    t0 = pkts[0][0]
    for t, d, _, slot, plen, size, payload, thread in rows:
        vals = []
        for off in range(0, min(len(payload), 96) - 3, 4):
            f = struct.unpack_from("<f", payload, off)[0]
            vals.append(f"{off:02X}:{f:.2f}" if 1e-3 < abs(f) < 1e7 else f"{off:02X}:-")
        print(f"{(t - t0) / 1e6:9.3f} " + " ".join(vals))


# P8 S2 (docs/research/pendientes/p8-2322-observador.md): one block per loading cycle of the
# capture's client (a "trip" starts with each MoveFieldAck 2174): 2549 / 2144 / 3066 / 2322 / 2445 / 2762
# with their record number (#n, from 1, as in the other P8 documents) and time, and the order checks of
# the acceptance criteria: exactly one 2322, after its 2144 (and its 3066).
TRIP_OPS = {2174: "MoveFieldAck", 2549: "MoveFieldCompleteReq", 2144: "MoveFieldCompleteAck",
            3066: "MoveFieldCompletePostAck", 2322: "PlayAllPreLoadSequencerAck", 2445: "LoadingCompleteReq",
            2762: "LoadingCompleteAck", 2042: "ScopeSenderAck"}


def trips(path):
    data = Path(path).read_bytes()
    if data[:4] != b"CDPK":
        raise SystemExit("not a cdmp capture")
    pos, n, t0, cur, all_trips = 12, 0, None, None, []
    while pos + REC.size <= len(data):
        t, d, _, size, copied, _, sess, thread = REC.unpack_from(data, pos)
        pos += REC.size
        pkt = data[pos:pos + copied]
        pos += copied
        n += 1
        t0 = t if t0 is None else t0
        if len(pkt) < 2:
            continue
        op = struct.unpack_from("<H", pkt, 0)[0]
        if op not in TRIP_OPS:
            continue
        if op == 2174:
            cur = {"events": []}
            all_trips.append(cur)
        if cur is None:
            continue
        extra = ""
        if op == 2322 and len(pkt) >= 9:
            extra = f" actor 0x{struct.unpack_from('<I', pkt, 5)[0]:08X}"
        if op == 2174 and len(pkt) >= 18:
            extra = f" owner 0x{struct.unpack_from('<I', pkt, 14)[0]:08X}"
        cur["events"].append((n, (t - t0) / 1e6, d, op, extra))
    for i, tr in enumerate(all_trips, 1):
        ev = tr["events"]
        print(f"trip {i}:")
        for rn, t, d, op, extra in ev:
            print(f"  #{rn:<8} t={t:12.6f} {'C>S' if d == 1 else 'S>C'} {op:5} {TRIP_OPS[op]}{extra}")
        first = {}
        for rn, t, d, op, _ in ev:
            first.setdefault(op, (rn, t))
        n2322 = sum(1 for e in ev if e[3] == 2322)
        if not n2322:
            verdict = "NO 2322"
        elif n2322 > 1:
            verdict = f"DUPLICATE: {n2322} x 2322"
        elif 2144 not in first or first[2322][0] < first[2144][0]:
            verdict = "ORDER: 2322 before its 2144"
        else:
            gap = (first[2322][1] - first[2144][1]) * 1e3
            after3066 = 3066 in first and first[3066][0] < first[2322][0]
            verdict = f"one 2322, {gap:.3f} ms after its 2144" + (", after its 3066" if after3066 else ", NOT after a 3066")
        print(f"  -> {verdict}; 2445 {'yes' if 2445 in first else 'no'}, 2762 {'yes' if 2762 in first else 'no'}")


# P8 S3, observer_hold_acks (docs/research/pendientes/p8-escenas-y-acks.md §3): the per-actor S>C packets
# (actor key u32 at payload +0) a client got for an NPC key BEFORE that key's first ActorLoadDataAck 2005 - a
# client drops them (no actor yet). Session 6B (no observer_hold_acks): 465 x 2723 and 1374 x 2404 for the 465
# keys observer_hold released. With observer_hold_acks=1 on the host, "held keys" (keys with a 2723 before their
# 2005) must be 0 and the 2723/2404 must follow the 2005.
HELD_OPS = (2723, 2404, 2586, 2991, 2697, 2642, 2506, 2402, 2645, 2434)


def announce_keys(payload):
    p, out = 0, []
    while p + 2 <= len(payload):
        ln = struct.unpack_from("<H", payload, p)[0]
        if ln == 0 or p + 2 + ln > len(payload):
            break
        if ln >= 5 and payload[p + 2] != 0:
            out.append(struct.unpack_from("<I", payload, p + 3)[0])
        p += 2 + ln
    return out


def acks(pkts, names):
    first = {}
    for i, p in enumerate(pkts):
        if p[1] == 2 and p[2] == 2005:
            for k in announce_keys(p[6]):
                first.setdefault(k, i)
    before, after_first, keys_before, keys_2723 = Counter(), Counter(), defaultdict(set), set()
    follows = Counter()  # the first held op right after a key's 2005 (same key)
    for i, p in enumerate(pkts):
        if p[1] != 2 or p[2] not in HELD_OPS or len(p[6]) < 4:
            continue
        k = struct.unpack_from("<I", p[6], 0)[0]
        if (k >> 28) != 0xB or k not in first:
            continue
        if i < first[k]:
            before[p[2]] += 1
            keys_before[p[2]].add(k)
            if p[2] == 2723:
                keys_2723.add(k)
        else:
            after_first[p[2]] += 1
    print(f"{len(first)} keys with a 2005")
    print("per-actor packets BEFORE their key's first 2005 (dropped by the client):")
    for op in HELD_OPS:
        if before[op] or after_first[op]:
            print(f"  {names.get(op, '?'):38} {op:5} before {before[op]:7} ({len(keys_before[op])} keys) | after {after_first[op]}")
    print(f"-> keys with a 2723 BEFORE their 2005: {len(keys_2723)} "
          f"({'the observer_hold side effect' if keys_2723 else 'none: every spawn action follows its 2005'})")


# P8, observer_scene_copy (p8-escenas-y-acks.md §2): every sequencer this client heard of, by id: its 2981 /
# 2661 / 2111 / 2524 / 2810 (S>C) with the actor key they carry, and its C>S 2173/2591/2436. A 2661 for this
# client's character (actor_hex, e.g. A010031F) without a later 2810 is a scene its client never closed.
def scenes(pkts, names, me=None):
    t0 = pkts[0][0]
    seqs = defaultdict(list)
    for t, d, op, slot, plen, size, payload, thread in pkts:
        if d == 2 and op in (2981, 2661, 2111, 2524) and len(payload) >= 13 and payload[0] == 4:
            actor, seq = struct.unpack_from("<I", payload, 1)[0], struct.unpack_from("<Q", payload, 5)[0]
            if op == 2661 and len(payload) >= 17:
                seq = struct.unpack_from("<Q", payload, 9)[0]
            seqs[seq].append(((t - t0) / 1e6, "S>C", op, actor))
        elif d == 2 and op == 2810 and len(payload) == 17:
            actor, seq = struct.unpack_from("<I", payload, 0)[0], struct.unpack_from("<Q", payload, 4)[0]
            seqs[seq].append(((t - t0) / 1e6, "S>C", op, actor))
        elif d == 1 and op in (2173, 2591, 2436, 2767) and len(payload) >= 8:
            seqs[struct.unpack_from("<Q", payload, 0)[0]].append(((t - t0) / 1e6, "C>S", op, 0))
    for seq in sorted(seqs):
        ev = seqs[seq]
        prepared = [e for e in ev if e[2] == 2661]
        if not prepared:
            continue
        mine = me is None or any(e[3] == me for e in prepared)
        if not mine:
            continue
        closed = [e for e in ev if e[2] == 2810 and e[0] >= prepared[0][0]]
        c = Counter((e[1], e[2]) for e in ev)
        summary = ", ".join(f"{d} {op} x{n}" for (d, op), n in sorted(c.items()))
        verdict = (f"CLOSED by 2810 (actor 0x{closed[0][3]:08X}) +{closed[0][0] - prepared[0][0]:.3f} s"
                   if closed else "NOT CLOSED: no 2810 after its 2661 (still live at the end, or never closed)")
        print(f"sequencer 0x{seq:X}: 2661 t={prepared[0][0]:.3f} actor 0x{prepared[0][3]:08X} | {summary} -> {verdict}")
# P8, menus without the pause (docs/research/pendientes/p8-pausa-esc.md, "Implementado", T2): the server's
# game-clock tick in each ServerTimeInfoAck 2188 (u32 0 | u64 ms at +4), sent every 5 s of wall time by the
# real-clock timer 1022. With a menu pausing the host's world the tick stands still (pausa-copia-congelada.md
# 1.1); with ui_pause_block it keeps advancing about as fast as the wall clock.
def ticks(pkts):
    rows = [p for p in pkts if p[2] == 2188 and p[1] == 2 and len(p[6]) >= 12]
    if not rows:
        print("no ServerTimeInfoAck 2188 in this capture")
        return
    t0 = pkts[0][0]
    prev = None
    frozen = slow = 0
    print("    t (s)   server tick ms   d tick ms   d wall ms   ratio")
    for t, d, _, slot, plen, size, payload, thread in rows:
        tick = struct.unpack_from("<Q", payload, 4)[0]
        line = f"{(t - t0) / 1e6:9.3f} {tick:16}"
        if prev:
            dt, dw = tick - prev[1], (t - prev[0]) / 1e3
            ratio = dt / dw if dw > 0 else 0.0
            mark = ""
            if dt == 0:
                mark, frozen = "  FROZEN", frozen + 1
            elif ratio < 0.5:
                mark, slow = "  SLOW", slow + 1
            line += f" {dt:11} {dw:11.0f} {ratio:7.2f}{mark}"
        print(line)
        prev = (t, tick)
    print(f"{len(rows)} ServerTimeInfoAck: {frozen} intervals FROZEN, {slow} SLOW (< half the wall clock)")


# P8 S15 (pendientes/p8-s15-viaje-tunel.md §8.1): the end of an actor record in an ActorLoadDataAck 2005 is
# u32 parent (actor+0xAC) | u16 n | n x u32 children (actor+0xB0). A child whose parent is neither alive on this
# client (an earlier 2005, no RemoveActorAck 2516 nor MoveFieldAck 2174 since) nor in the same 2005 is an ORPHAN: the client's pending-login
# loop destroys it (0x8B7D78) and the parent, when it comes, can never form its login group (+0x5E 8 for ever).
# The host's own client gets none (S15: 0); the guest got 75.
def actor_records(payload):
    p, out = 0, []
    while p + 2 <= len(payload):
        ln = struct.unpack_from("<H", payload, p)[0]
        if ln == 0 or p + 2 + ln > len(payload):
            break
        out.append(payload[p + 2:p + 2 + ln])
        p += 2 + ln
    return out


def record_family(rec):
    def plausible(k):
        return (k >> 28) in (0xA, 0xB)
    for n in range(0, 33):
        if len(rec) < 6 + 4 * n or struct.unpack_from("<H", rec, len(rec) - 2 - 4 * n)[0] != n:
            continue
        kids = [struct.unpack_from("<I", rec, len(rec) - 4 * n + 4 * i)[0] for i in range(n)]
        parent = struct.unpack_from("<I", rec, len(rec) - 6 - 4 * n)[0]
        if all(plausible(k) for k in kids) and (parent == 0 or plausible(parent)):
            return parent, kids
    return None


def orphans(pkts):
    t0 = pkts[0][0]
    alive, first, found = {}, {}, []
    for t, d, op, slot, plen, size, payload, thread in pkts:
        if d != 2:
            continue
        tt = (t - t0) / 1e6
        if op == 2516 and len(payload) >= 10:
            # every key of the 2516 (u8 | u32 | u8 reason | n x u32), as the host's counter (child_hold.cpp)
            for i in range(6, len(payload) - 3, 4):
                alive.pop(struct.unpack_from("<I", payload, i)[0], None)
            continue
        if op == 2174:
            # MoveFieldAck: a field change, the client starts empty (as the host's counter; on the S14/S15
            # captures this changes no count)
            alive.clear()
            continue
        if op != 2005:
            continue
        recs = [(r[0], struct.unpack_from("<I", r, 1)[0], record_family(r) if r[0] else None)
                for r in actor_records(payload) if len(r) >= 5]
        here = {k for _, k, _ in recs}
        for typ, k, fam in recs:
            first.setdefault(k, tt)
            if fam and fam[0] and fam[0] not in alive and fam[0] not in here:
                found.append((tt, typ, k, fam[0]))
        for _, k, _ in recs:
            alive[k] = tt
    print(f"{len(found)} orphan child record(s) (parent neither alive on this client nor in the same 2005)")
    for tt, typ, k, parent in found:
        ps = first.get(parent)
        when = f"t={ps:.3f} (+{ps - tt:.3f} s)" if ps is not None else "never"
        print(f"  t={tt:9.3f} type {typ} child {k:08X} parent {parent:08X} | the parent's first 2005 {when}")


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    names = load_names()
    pkts = read_capture(sys.argv[1])
    cmd = sys.argv[2]
    if cmd == "trips":
        trips(sys.argv[1])
        return
    if cmd == "acks":
        acks(pkts, names)
        return
    if cmd == "orphans":
        orphans(pkts)
        return
    if cmd == "scenes":
        scenes(pkts, names, int(sys.argv[3], 16) if len(sys.argv) > 3 else None)
        return
    if cmd == "summary":
        summary(pkts, names)
    elif cmd == "phases":
        phases(pkts, names)
    elif cmd == "show":
        show(pkts, names, resolve(sys.argv[3], names), int(sys.argv[4]) if len(sys.argv) > 4 else 10)
    elif cmd == "floats":
        floats(pkts, names, resolve(sys.argv[3], names))
    elif cmd == "ticks":
        ticks(pkts)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
