"""cdmp: servidor MCP (stdio) de solo lectura sobre el proyecto de Ghidra de CrimsonDesert.exe 2976.

Capa fina sobre el servidor de consultas de Ghidra (Cdmp_Query.java "serve", 127.0.0.1:18490,
abierto con -noanalysis -readOnly). Solo usa la biblioteca estándar de Python: no instala nada.

- Transporte: MCP por stdio (JSON-RPC 2.0, un mensaje por línea). Este proceso NO abre ningún
  puerto: solo se conecta como cliente a 127.0.0.1 (literal, no configurable).
- Peticiones al servidor de Ghidra: campos separados por TAB, validados aquí y otra vez en Java.
  Argumentos de más, tipos incorrectos, números fuera de rango o caracteres de control se rechazan.
- Solo lectura: las herramientas son consultas; ninguna renombra, comenta, analiza ni ejecuta
  scripts. Lo único que se escribe es el .c de cada decompilación (exports\\decomp).
- Propiedad del proceso: si este MCP levanta el servidor, recuerda su id de instancia y su PID de
  Java, y al terminar (fin de stdin, error o excepción: try/finally) lo para y comprueba que ese PID
  termina (server-stop.ps1). Un servidor que ya estaba levantado no es suyo y no lo para al salir.
- Sin autenticación: cualquier proceso local puede consultar o parar el servidor de Ghidra.

Uso (lo lanza Claude Code):  python -I D:\\RE\\CrimsonDesert_2976\\scripts\\cdmp_mcp.py
Variables opcionales: CDMP_PORT (18490), CDMP_MCP_MAXCHARS (60000), CDMP_MCP_LOG (fichero de log).
"""
import json
import os
import re
import socket
import subprocess
import sys
import time

HOST = "127.0.0.1"  # literal: nunca otra dirección
PORT = int(os.environ.get("CDMP_PORT", "18490"))
if not 1024 <= PORT <= 65535:
    raise SystemExit("CDMP_PORT fuera de rango")
MAXCHARS = max(1000, min(200_000, int(os.environ.get("CDMP_MCP_MAXCHARS", "60000"))))
MAX_RESPONSE_BYTES = 2 * 1024 * 1024   # lo que se lee del socket como máximo
MAX_STDIN_LINE = 1024 * 1024           # mensaje JSON-RPC más largo que se acepta
MAX_FIELD = 512
DISASM_MAX = 400
SCRIPTS = os.path.dirname(os.path.abspath(__file__))
LOG = os.environ.get("CDMP_MCP_LOG")
PROTOCOL_VERSIONS = ("2025-06-18", "2025-03-26", "2024-11-05")
SERVER_INFO = {"name": "cdmp-ghidra", "version": "1.1.0"}
INSTANCE_RE = re.compile(r"^[0-9a-f]{32}$")
CONTROL_RE = re.compile(r"[\x00-\x1f\x7f]")

# Servidor levantado por este proceso: {"instance": str, "pid": int} o None.
OWNED = {"server": None}

BUILD_NOTE = ("CrimsonDesert.exe build 1.0.0.2976 (PE ts 0x6AB28F00, base 0x140000000). "
              "Direcciones como RVA (0x35CA00), VA (0x14035CA00) o nombre (ClientNet::PostPacket, "
              "cdmp_SentryReporter). Los nombres del equipo llevan prefijo cdmp_ (Clase::metodo -> "
              "cdmp_Clase__metodo). .rsrc es el código real; .xtls está virtualizado (no se analiza).")


def log(msg):
    if LOG:
        try:
            with open(LOG, "a", encoding="utf-8") as f:
                f.write(time.strftime("%Y-%m-%d %H:%M:%S ") + msg + "\n")
        except OSError:
            pass


class BadArgs(Exception):
    pass


# --------------------------------------------------------------------------- servidor de Ghidra
def ghidra(fields, timeout=180.0):
    """Envía una petición (lista de campos) y devuelve la respuesta, o None si no hay servidor."""
    for f in fields:
        if not isinstance(f, str) or not f or len(f) > MAX_FIELD or CONTROL_RE.search(f):
            raise BadArgs("campo inválido")
    data = ("\t".join(fields) + "\n").encode("utf-8")
    try:
        s = socket.create_connection((HOST, PORT), timeout=1.0)
    except OSError:
        return None
    with s:
        s.settimeout(timeout)
        s.sendall(data)
        chunks, total = [], 0
        while total < MAX_RESPONSE_BYTES:
            b = s.recv(65536)
            if not b:
                break
            chunks.append(b)
            total += len(b)
    txt = b"".join(chunks)[:MAX_RESPONSE_BYTES].decode("utf-8", errors="replace")
    if total >= MAX_RESPONSE_BYTES:
        txt += "\n... (cortado en %d bytes por el MCP)\n" % MAX_RESPONSE_BYTES
    return txt


def ping():
    """Identidad del servidor que responde: dict(program, sha256, pid, instance) o None."""
    r = ghidra(["ping"], timeout=10)
    if not r:
        return None
    p = r.split("\n", 1)[0].rstrip("\r").split("\t")
    if len(p) < 5 or p[0] != "pong" or not p[3].isdigit():
        return None
    return {"program": p[1], "sha256": p[2], "pid": int(p[3]), "instance": p[4]}


def powershell(script, args, timeout):
    exe = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32", "WindowsPowerShell", "v1.0",
                       "powershell.exe")
    r = subprocess.run([exe, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", os.path.join(SCRIPTS, script)] + args,
                       capture_output=True, timeout=timeout, stdin=subprocess.DEVNULL,
                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    out = (r.stdout or b"").decode("utf-8", errors="replace") + (r.stderr or b"").decode("utf-8", errors="replace")
    return out.strip(), r.returncode


def pid_alive(pid):
    """True si el proceso existe (Windows, kernel32 vía ctypes de la biblioteca estándar)."""
    import ctypes
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    h = k.OpenProcess(0x00100000 | 0x1000, False, pid)  # SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return False
    try:
        return k.WaitForSingleObject(h, 0) == 0x102  # WAIT_TIMEOUT: sigue vivo
    finally:
        k.CloseHandle(h)


def new_instance():
    return os.urandom(16).hex()


def start_server():
    cur = ping()
    if cur:
        return ("Ya había un servidor levantado (pid Java %d, instancia %s). No es de esta sesión: no se "
                "parará al salir." % (cur["pid"], cur["instance"] or "?")), False
    inst = new_instance()
    out, rc = powershell("server-start.ps1", ["-Instance", inst], timeout=960)
    cur = ping()
    if rc == 0 and cur and cur["instance"] == inst:
        OWNED["server"] = {"instance": inst, "pid": cur["pid"]}
        log("servidor propio: instancia %s pid %d" % (inst, cur["pid"]))
        return out + "\n(Es de esta sesión: se parará solo al cerrarla, o con ghidra_server_stop.)", False
    return "No se pudo levantar el servidor propio.\n" + out, True


def stop_server(only_owned=False):
    """Para el servidor y comprueba que su Java termina. Devuelve (texto, error)."""
    own = OWNED["server"]
    if only_owned and not own:
        return "", False
    cur = ping()
    if not cur:
        if own and pid_alive(own["pid"]):
            text, err = "El servidor propio no responde pero su Java (pid %d) sigue vivo." % own["pid"], True
        else:
            text, err = "No había servidor levantado.", False
        OWNED["server"] = None
        return text, err
    if only_owned and cur["instance"] != own["instance"]:
        OWNED["server"] = None
        return "El servidor que responde no es el de esta sesión; no se para.", False
    args = ["-Instance", cur["instance"]] if INSTANCE_RE.match(cur["instance"] or "") else []
    out, rc = powershell("server-stop.ps1", args, timeout=180)
    alive = pid_alive(cur["pid"])
    if not alive:
        OWNED["server"] = None
    return out + ("\nJava pid %d: %s." % (cur["pid"], "SIGUE VIVO" if alive else "terminado")), (rc != 0 or alive)


# --------------------------------------------------------------------------- herramientas
TARGET_DESC = "RVA (0x35CA00), VA (0x14035CA00) o nombre de símbolo/función (ClientNet::PostPacket, cdmp_SentryReporter)."


def p_str(desc, min_len=1):
    return {"type": "string", "description": desc, "minLength": min_len, "maxLength": MAX_FIELD}


TOOLS = {
    "ghidra_decompile": ("decomp", "Pseudocódigo C (decompilador de Ghidra) de la función que contiene la dirección. "
                         "Incluye los comentarios cdmp: del equipo.", {"target": p_str(TARGET_DESC)}, ["target"]),
    "ghidra_disassemble": ("disasm", "Desensamblado (instrucciones con bytes, etiquetas y referencias) desde una dirección.",
                           {"target": p_str(TARGET_DESC),
                            "count": {"type": "integer", "minimum": 1, "maximum": DISASM_MAX,
                                      "description": "Número de instrucciones, de 1 a %d (por defecto 40)." % DISASM_MAX}},
                           ["target"]),
    "ghidra_xrefs": ("xrefs", "Referencias a una dirección (como mucho 2000): quién llama a la función o lee/escribe el "
                     "dato, con la función e instrucción de origen.", {"target": p_str(TARGET_DESC)}, ["target"]),
    "ghidra_function": ("func", "Datos de una función: entrada, tamaño, firma, nombres, comentario, llamadores y llamados.",
                        {"target": p_str(TARGET_DESC)}, ["target"]),
    "ghidra_strings": ("strings", "Cadenas definidas que contienen el texto (sin distinguir mayúsculas; ASCII, UTF-8 con "
                       "coreano y UTF-16) y las funciones que las usan (como mucho 200).",
                       {"text": p_str("Texto a buscar (2 caracteres o más).", 2)}, ["text"]),
    "ghidra_symbols": ("symbols", "Símbolos con nombre (funciones, etiquetas, vtables; no FUN_/DAT_) que contienen el texto "
                       "(como mucho 200).", {"text": p_str("Texto a buscar (2 caracteres o más).", 2)}, ["text"]),
    "ghidra_vtable": ("vtable", "Métodos (punteros) de la tabla virtual que empieza en la dirección (como mucho 1024).",
                      {"target": p_str("Dirección de la vtable (RVA, VA o nombre).")}, ["target"]),
    "ghidra_class": ("class", "Vtables de las clases RTTI cuyo nombre coincide (exacto o, si no hay, que lo contenga).",
                     {"name": p_str("Nombre de clase RTTI, p. ej. NwVirtualAsyncSession.", 2)}, ["name"]),
    "ghidra_imports": ("imports", "Dónde se llama a una importación (nombre que contiene el texto), p. ej. "
                       "sentry_capture_event_with_scope.", {"name": p_str("Nombre (o parte) de la importación.", 2)}, ["name"]),
    "ghidra_server_status": (None, "Dice si el servidor de Ghidra (127.0.0.1:%d, solo lectura) está levantado, su PID de "
                             "Java e instancia, y si es de esta sesión." % PORT, {}, []),
    "ghidra_server_start": (None, "Levanta el servidor de Ghidra de solo lectura (proceso aparte, ~15 s, hasta 12 GB de "
                            "memoria). Se para solo al cerrar esta sesión. Mientras está levantado, el proyecto no se "
                            "puede abrir en la interfaz de Ghidra.", {}, []),
    "ghidra_server_stop": (None, "Para el servidor de Ghidra y comprueba que su proceso Java termina.", {}, []),
}


def tool_list():
    out = []
    for name, (_, desc, props, req) in TOOLS.items():
        out.append({"name": name, "description": desc + (" " + BUILD_NOTE if props else ""),
                    "inputSchema": {"type": "object", "properties": props, "required": req, "additionalProperties": False},
                    "annotations": {"readOnlyHint": name not in ("ghidra_server_start", "ghidra_server_stop"),
                                    "openWorldHint": False}})
    return out


def validate(name, args):
    """Comprueba los argumentos contra el esquema; devuelve los campos de la petición."""
    cmd, _, props, req = TOOLS[name]
    if not isinstance(args, dict):
        raise BadArgs("'arguments' debe ser un objeto")
    extra = sorted(set(args) - set(props))
    if extra:
        raise BadArgs("argumentos no admitidos: %s" % ", ".join(extra))
    for r in req:
        if r not in args:
            raise BadArgs("falta el argumento '%s'" % r)
    fields = [cmd] if cmd else []
    for key in props:  # orden del esquema: el texto primero, el número después
        if key not in args:
            continue
        v, spec = args[key], props[key]
        if spec["type"] == "string":
            if not isinstance(v, str):
                raise BadArgs("'%s' debe ser texto" % key)
            v = v.strip()
            if len(v) < spec["minLength"] or len(v) > spec["maxLength"]:
                raise BadArgs("'%s' debe tener entre %d y %d caracteres" % (key, spec["minLength"], spec["maxLength"]))
            if CONTROL_RE.search(v):
                raise BadArgs("'%s' contiene caracteres de control" % key)
            fields.append(v)
        elif spec["type"] == "integer":
            if isinstance(v, bool) or not isinstance(v, int):
                raise BadArgs("'%s' debe ser un entero" % key)
            if not spec["minimum"] <= v <= spec["maximum"]:
                raise BadArgs("'%s' debe estar entre %d y %d" % (key, spec["minimum"], spec["maximum"]))
            fields.append(str(v))
    return fields


def call_tool(name, args):
    if name not in TOOLS:
        return "Herramienta desconocida.", True
    try:
        fields = validate(name, args)
    except BadArgs as e:
        return "Argumentos rechazados: %s." % e, True
    if name == "ghidra_server_status":
        cur = ping()
        if not cur:
            return NOT_RUNNING, False
        own = OWNED["server"]
        mine = bool(own and own["instance"] == cur["instance"])
        return ("Levantado en %s:%d: %s, pid Java %d, instancia %s, sha256 %s. %s" % (
            HOST, PORT, cur["program"], cur["pid"], cur["instance"] or "?", cur["sha256"],
            "Es de esta sesión." if mine else "No es de esta sesión.")), False
    if name == "ghidra_server_start":
        return start_server()
    if name == "ghidra_server_stop":
        return stop_server()
    res = ghidra(fields)
    if res is None:
        return NOT_RUNNING, True
    is_err = res.lstrip("\ufeff").startswith("ERROR")
    if len(res) > MAXCHARS:
        res = res[:MAXCHARS] + "\n... (salida recortada a %d caracteres; afina la consulta)\n" % MAXCHARS
    return res, is_err


NOT_RUNNING = ("El servidor de Ghidra no está levantado en 127.0.0.1:%d. Llama a ghidra_server_start (tarda ~15 s) "
               "o ejecuta D:\\RE\\CrimsonDesert_2976\\scripts\\server-start.ps1." % PORT)


# --------------------------------------------------------------------------- JSON-RPC por stdio
def send(msg):
    sys.stdout.write(json.dumps(msg, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def error(rid, code, msg):
    return {"jsonrpc": "2.0", "id": rid, "error": {"code": code, "message": msg}}


def handle(req):
    """Devuelve el mensaje de respuesta (dict) o None para notificaciones."""
    if not isinstance(req, dict):
        return error(None, -32600, "La petición debe ser un objeto JSON")
    rid = req.get("id")
    if rid is not None and (isinstance(rid, bool) or not isinstance(rid, (int, str))):
        return error(None, -32600, "id inválido")
    method = req.get("method")
    if req.get("jsonrpc") != "2.0" or not isinstance(method, str):
        return error(rid, -32600, "Petición JSON-RPC 2.0 inválida")
    params = req.get("params", {})
    if params is None:
        params = {}
    if not isinstance(params, dict):
        return error(rid, -32602, "'params' debe ser un objeto")
    notification = "id" not in req
    if method == "initialize":
        want = params.get("protocolVersion")
        ver = want if want in PROTOCOL_VERSIONS else PROTOCOL_VERSIONS[0]
        result = {"protocolVersion": ver, "capabilities": {"tools": {"listChanged": False}}, "serverInfo": SERVER_INFO,
                  "instructions": "Consultas de solo lectura sobre el proyecto de Ghidra de CrimsonDesert.exe. " + BUILD_NOTE +
                                  " Si una herramienta dice que el servidor no está levantado, usa ghidra_server_start."}
    elif method == "ping":
        result = {}
    elif method == "tools/list":
        result = {"tools": tool_list()}
    elif method == "tools/call":
        name = params.get("name")
        args = params.get("arguments", {})
        if not isinstance(name, str):
            return error(rid, -32602, "'name' debe ser texto")
        if args is None:
            args = {}
        t0 = time.time()
        try:
            text, is_err = call_tool(name, args)
        except Exception as e:  # noqa: BLE001 - se devuelve al cliente como error de la herramienta
            text, is_err = "Error en %s: %s" % (name, type(e).__name__), True
        log("%s %s -> %s %.2fs" % (name, json.dumps(args, ensure_ascii=False)[:300], "ERR" if is_err else "ok", time.time() - t0))
        result = {"content": [{"type": "text", "text": text}], "isError": bool(is_err)}
    elif method == "resources/list":
        result = {"resources": []}
    elif method == "prompts/list":
        result = {"prompts": []}
    elif notification:
        return None  # p. ej. notifications/initialized
    else:
        return error(rid, -32601, "Método no soportado: %s" % method[:80])
    return None if notification else {"jsonrpc": "2.0", "id": rid, "result": result}


def main():
    sys.stdin.reconfigure(encoding="utf-8", errors="replace")
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    log("arranque pid=%d %s:%d" % (os.getpid(), HOST, PORT))
    try:
        while True:
            line = sys.stdin.readline(MAX_STDIN_LINE + 1)
            if not line:
                break  # EOF: Claude Code cerró la sesión
            if len(line) > MAX_STDIN_LINE:
                send(error(None, -32600, "Mensaje demasiado largo"))
                while line and not line.endswith("\n"):  # descarta el resto de esa línea
                    line = sys.stdin.readline(MAX_STDIN_LINE + 1)
                continue
            line = line.strip()
            if not line:
                continue
            try:
                req = json.loads(line)
            except (json.JSONDecodeError, RecursionError):
                send(error(None, -32700, "JSON inválido"))
                continue
            try:
                resp = handle(req)
            except Exception as e:  # noqa: BLE001
                resp = error(None, -32603, "Error interno: %s" % type(e).__name__)
            if resp is not None:
                send(resp)
    finally:
        if OWNED["server"]:
            try:
                text, err = stop_server(only_owned=True)
                log("parada al salir: %s" % text.replace("\n", " | "))
            except Exception as e:  # noqa: BLE001
                log("parada al salir falló: %r" % e)
        log("fin")


if __name__ == "__main__":
    main()
