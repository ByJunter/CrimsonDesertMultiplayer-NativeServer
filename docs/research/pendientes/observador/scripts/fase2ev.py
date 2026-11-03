# Fase 2 (p8-observador-escenas.md): extractos de desensamblado del enrutado de los Ack de escena en el CLIENTE.
# Solo lectura del PE autorizado; uso: py -I fase2ev.py > fase2-cliente-enrutado.txt
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D


def rng(title, a, b):
    print()
    print(f"=== {title}  [{a:X}..{b:X})")
    for line in D.dis(a, b - a, stop_ret=False):
        print(line)


def slots(title, vt, idx):
    print()
    print(f"=== {title}: vtable exe+0x{vt:X}")
    for i in idx:
        p = struct.unpack('<Q', D.rd(vt + 8 * i, 8))[0] - 0x140000000
        print(f"slot {i:3d} +0x{8 * i:03X}: {p:08X}")


print("Fase 2: enrutado de los Ack de escena en el CLIENTE (build 2976, PE autorizado, capstone 5.0.7). "
      "RTTI: tools/scripts/rtti_dump.py")
rng("2661 handler 0xC05BB0: actor por clave (0x8A67E0) -> [r12]; contexto de campo 0x17FA6C0; vf48; kind==4 -> 0xA9CA40",
    0xC0607F, 0xC0618D)
rng("2661: lectura de cada enlace (u32 nodo, u32 actor, u8, u8, u8)", 0xC05EA0, 0xC05FBF)
rng("0x17FA6C0(actor, out) = [[actor+0x68]+0x1A0]->vf140(out)", 0x17FA6C0, 0x17FA6FB)
print()
print("(vf140 que construye FieldAttacherForActor 0x5912E60: un solo implementador en todas las vtables RTTI -> "
      "pa::ClientTransformSyncActorComponent; slotref.py 40 5912E60)")
rng("ClientTransformSyncActorComponent::vf140 0x9F8560: si actor+0xC0/+0xC2, campo = [[[exe+0x6D69190]+0xC8]] "
    "(el campo ACTUAL, global, no el del actor)", 0x9F8560, 0x9F86DB)
slots("pa::ClientField", 0x55BCEA0, [8, 9, 10])
print("-> ClientField::vf48 = 0x9A5BE0: gestor [[[pj+0x68]+0xA8]+0x18] del personaje del usuario [field+0x180] "
      "(decomp 0x9a5be0.c)")
rng("0xA9CA40: crea la instancia (0xC0 B, 0xA90AF0) y recorre los enlaces: tipo 4/5 -> 0x8AF170; no encontrado -> 0xA9CE81",
    0xA9CA40, 0xA9CC20)
rng("0xA9CA40: actor ausente 0xA9CE81 (codigo [exe+0x6CDB9E4]; destruye la instancia)", 0xA9CE81, 0xA9CF2B)
rng("0xA9CA40: alta en el mapa del gestor +0x170, clave u64 = play data +8 (hash de 8 B); duplicado -> [exe+0x6CF76A4]",
    0xA9D23C, 0xA9D2D4)
slots("pa::ClientSequencerStageManager", 0x55C9720, [0, 1, 9])
rng("ClientSequencerStageManager slot 1 0xA94660 (vaciado: mapa +0x170..+0x188, +0x190..+0x1A8, listas +0x1B0/+0x1C0/+0x1D0)",
    0xA94660, 0xA94700)
rng("NPC: ClientSequencerPlayDataActorComponent::vf13 0x9ED290 busca la instancia en el gestor del actor comp+0x2D8",
    0x9ED401, 0x9ED48C)
rng("2981 handler 0xC073A0: mismo enrutado (0x17FA6C0, vf48) -> 0xA9E100", 0xC0770A, 0xC07776)
rng("2810 handler 0xC06330: mismo enrutado -> 0xA9D950", 0xC0655F, 0xC065BA)
rng("2264 handler 0xC0DF10: mismo enrutado -> 0xAA1E60", 0xC0E1F0, 0xC0E27E)
rng("2322 handler 0xB87890: mismo enrutado -> [M+0x231] = 2", 0xB87A12, 0xB87A6E)
rng("Servidor, 2264: 0x2885BD0 lo envia con ToTargetSendTargetFinder (vt 0x5B25438) al actor del gestor [r15+0x18]",
    0x2886FBF, 0x288703A)
