"""Checks that the login scripts in docs/research/data/*.troc are what
make_troc_script.py makes from login_script_112627.json today (line ends
ignored), so the JSON, the generator and the scripts stay in sync.
Run by ctest (optd_scripts); exit code 1 names the scripts that differ.

The hand-made variants login_e3lc00act*_112627.troc (C1c-C1f, obsolete) are
not generated and not checked.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "..", "docs", "research", "data")
GEN = os.path.join(HERE, "make_troc_script.py")
JSON = os.path.join(DATA, "login_script_112627.json")

C4A = ["--comment", "C4a (D-49): the E3 login with the first action (4b), then user 2 says goodbye as the game's "
                    "client does",
       "--comment", "(LogoutSessionToServerReq 2001, 3 bytes) and waits for the native logout Acks 2003 and 2002."]
D53R = ["--comment", "D-53r: login_e3lc00bye_112627 plus the first action of user 2's own child (type 6) after the "
                     "shared load, as",
        "--comment", "the real client does (112627 #3449): its key, layer-0 hash and user 2's position are learned "
                     "from the server."]
LC00 = ["--stage", "E3", "--loading-complete", "00"]


def cut(step, *comment):
    """Mid-load link loss (docs/research/pendientes/salida-en-carga.md)."""
    return LC00 + ["--end-after-step", step] + [x for c in comment for x in ("--comment", c)]


SCRIPTS = {
    "login_e2_112627.troc": ["--stage", "E2"],
    "login_e3_112627.troc": ["--stage", "E3"],
    "login_e3opt_112627.troc": ["--stage", "E3", "--optional"],
    "login_e3lc00_112627.troc": ["--stage", "E3", "--loading-complete", "00"],
    "login_e3lc00bye_112627.troc": ["--stage", "E3", "--loading-complete", "00", "--goodbye-after-ms", "20000"] + C4A,
    "login_e3lc00bye_child_112627.troc": ["--stage", "E3", "--loading-complete", "00", "--child-action",
                                          "--goodbye-after-ms", "20000"] + D53R,
    "login_e3lc00cut3_112627.troc": cut(
        "3", "Mid-load A, parking: the E3 login cut right after the parking MoveFieldAck (type 3), before its",
        "MoveFieldCompleteReq; user 2 then vanishes and the host's C4 hands the 2001 (salida-en-carga.md)."),
    "login_e3lc00cut8_112627.troc": cut(
        "8", "Mid-load A, shared (as the P8 guest of 2026-10-09 16:19:53): the E3 login cut right after the shared",
        "MoveFieldAck (GameLoadingStartReq), before MoveFieldCompleteReq; then the link is lost (salida-en-carga.md)."),
    "login_e3lc00cut9_112627.troc": cut(
        "9", "Mid-load B, shared: the E3 login cut right after the shared MoveFieldCompleteAck, before",
        "LoadingCompleteReq; then the link is lost (salida-en-carga.md)."),
}


def main():
    bad = []
    for name, args in SCRIPTS.items():
        made = subprocess.run([sys.executable, "-I", "-B", GEN, JSON] + args, capture_output=True, check=True,
                              text=True, encoding="utf-8").stdout.replace("\r\n", "\n")
        with open(os.path.join(DATA, name), encoding="utf-8", newline="") as f:
            have = f.read().replace("\r\n", "\n")
        print(f"{name}: {'same' if made == have else 'DIFFERENT'}")
        if made != have:
            bad.append(name)
    if bad:
        print("Regenerate with make_troc_script.py (flags in check_troc_scripts.py): " + ", ".join(bad))
        return 1
    print("All generated login scripts match the JSON")
    return 0


if __name__ == "__main__":
    sys.exit(main())
