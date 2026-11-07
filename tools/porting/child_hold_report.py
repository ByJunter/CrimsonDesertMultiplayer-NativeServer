"""Read-only build-3527 evidence for the native child publication hooks.

This validates instructions and PE function boundaries; it does not install hooks
or claim that the optional family/publication policies have been ported.
"""
import hashlib
import json
import struct

from signature_map import ROOT, Pe, ImageScanner


def main():
    data = (ROOT.parent / "bin64/CrimsonDesert.exe").read_bytes()
    expected = "dc4237f21d6ce1e49f8e5d6fa89bf803fbbe6549123078efdc393265c7bf6841"
    assert hashlib.sha256(data).hexdigest() == expected, "Different executable"
    pe = Pe(data)
    scanner = ImageScanner(pe)
    checks = [
        (0x2D96BB0, "48895c2408488974241048897c2418554154415541564157", "NotifyAppear function entry"),
        (0x2D96CBC, "498b1e418b4608", "sessions pointer and u32 count at +8"),
        (0x2D96D27, "498b0424488945f0418b442408", "actor keys pointer and u32 count at +8"),
        (0x2D96D3F, "e88c840000", "key-to-actor finder call, target 0x2D9F1D0"),
        (0x2D8F38B, "e820780000", "NotifyAppear new-observer caller"),
        (0x2D8F58C, "e81f760000", "NotifyAppear moving-user caller"),
        (0x2BE08C0, "48895c24085556574154415541564157488dac24d0fcffff4881ec40040000", "StartActionReq vf2 function entry"),
        (0x251905A, "498b4740c6403001", "CharacterControl restore state +0x40, restored byte +0x30"),
        (0x29AAF36, "b8eb010000c6041801", "server thread sets TLS realm +0x1EB"),
        (0x29AAF77, "e904000000", "server realm setup tail-jumps to ServerRunThreadLoop 0x29AAF80"),
        (0x2D9F20B, "41bceb010000", "NotifyAppear finder uses the same TLS realm +0x1EB"),
        (0x2D047AA, "488b4868488b99a0010000", "actor components +0x68, TransformSync slot +0x1A0"),
        (0x2D04801, "488b4308", "TransformSync actor backlink +8"),
        (0x2D04820, "488b9b48050000", "TransformSync current sector +0x548 in ToNearExceptOneControl finder"),
        (0x2D046F0, "488b9b48050000", "same sector offset in ToNearExceptOneSelf finder"),
        (0x2D8EF3F, "488b43688b4b7048c1e104", "sector observer pointer +0x68, count +0x70, stride 16"),
        (0x2D8EF5B, "4883c010", "sector observer iteration stride 16"),
        (0x2D8F6D5, "498b87a0000000", "actor owner +0xA0 in sector-change caller"),
        (0x2BE0CDB, "498b80880000000fb65001", "actor static info +0x88, type byte +1"),
        (0x2BE0D36, "488b4868", "actor components +0x68 for StartActionReq"),
        (0x2BE0D5D, "488b4940e8ba671200", "CharacterControl slot +0x40 passed to action logic 0x2D07520"),
        (0x2D07552, "488b5908", "CharacterControl actor backlink +8"),
        (0x2D0757F, "8b8b900000003908", "actor controller key +0x90 compared against the request user's key"),
    ]
    rows = []
    for rva, encoded, meaning in checks:
        b = bytes.fromhex(encoded)
        at = pe.rva_to_off(rva)
        assert data[at:at + len(b)] == b, f"Instruction changed: {rva:X} ({meaning})"
        function = scanner.function(rva)
        assert function, f"No function boundary for {rva:X}"
        rows.append({"rva": hex(rva), "bytes": encoded, "meaning": meaning,
                     "function": {"begin": hex(function["begin"]), "end": hex(function["end"])}})
    for rva in (0x2D96BB0, 0x2BE08C0):
        assert scanner.function(rva)["begin"] == rva, "Inline hook would be mid-function"
    slot = pe.rva_to_off(0x5EEC638 + 0x10)
    assert struct.unpack_from("<Q", data, slot)[0] == 0x140000000 + 0x2BE08C0
    out = {
        "build": 3527, "sha256": expected, "checks": rows,
        "rva_mapping": {
            "0x2B000C0": "0x2D96BB0", "0x2B001B7": "0x2D96CBC",
            "0x2B0021D": "0x2D96D27", "0x2B00233": "0x2D96D3F",
            "0x2AF90EB": "0x2D8F38B", "0x2AF92D1": "0x2D8F58C",
            "0x296CAA0": "0x2BE08C0", "0x5B251D8": "0x5EEC638",
            "0x2328890": "0x251905A", "0x27733A6": "0x29AAF36"},
        "changed_members": {"tls_server_realm": ["0x1EC", "0x1EB"],
                            "transform_sync_sector": ["0x540", "0x548"]},
        "integration_required": [
            "Use build profile for both hooks, all guards, vtable, caller return addresses and logs.",
            "Use session2 profile for SAM slot and ServerUserActor vtable, preserving test seams.",
            "Build-aware sector reader: preserve old-build and pure-test +0x540; use +0x548 at runtime for 3527.",
            "Use +0x1EB server realm before calling native NotifyAppear on 3527.",
            "Audit optional family parent/settle readers before enabling those policies.",
            "Run child-hold regression tests and live native publication validation before deployment."],
        "runtime_changes": False,
    }
    path = ROOT / "tools/porting/data/child_hold_3527.json"
    path.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(f"{len(rows)} instruction assertions, two entry boundaries and vf2 slot verified; {path}")


if __name__ == "__main__":
    main()
