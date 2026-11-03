// cdmp: consultas rápidas sobre el proyecto ya analizado (no reanaliza; usar con -noanalysis -readOnly).
//
// Petición estructurada: campos separados por TAB (U+0009), en UTF-8, una sola línea:
//   <comando> TAB <arg1> [TAB <arg2>]
// Nunca se interpreta texto libre como sintaxis: cada comando admite un número fijo de campos y
// cada campo se valida (longitud, caracteres de control, formato numérico); lo demás se rechaza.
//
// Comandos (campos):
//   decomp  <objetivo>              pseudocódigo C de la función que contiene la dirección
//   xrefs   <objetivo>              referencias a esa dirección (o a la entrada de la función)
//   strings <texto>                 cadenas que contienen el texto (sin mayúsculas) y quién las usa
//   vtable  <objetivo>              punteros de la tabla virtual que empieza ahí
//   class   <nombre RTTI>           tablas virtuales de las clases cuyo nombre coincide
//   imports <nombre>                llamadas a la importación (contiene, sin mayúsculas)
//   func    <objetivo>              entrada, tamaño, firma, llamadores/llamados
//   disasm  <objetivo> [<n>]        n instrucciones (1–400; 40 por defecto)
//   symbols <texto>                 símbolos con nombre (no FUN_/DAT_) que contienen el texto
//   ping                            "pong TAB programa TAB sha256 TAB pid TAB instancia"
//   shutdown                        (solo servidor) para el servidor
// <objetivo>: RVA (0x35CA00), VA (0x14035CA00) o nombre de símbolo/función.
//
// Modos (argumentos del script; los pone el lanzador, no vienen de la red):
//   req=<fichero> out=<fichero> [decompdir=<dir>]     una consulta: lee la petición del fichero
//   serve=<puerto> [instance=<32 hex>] [decompdir=<dir>]  servidor en 127.0.0.1 (literal)
//
// Ningún comando modifica el programa (además se abre con -readOnly). Escrituras: el .c de decomp
// en decompdir (nombre derivado de la RVA numérica) y, en modo req, el fichero out.
// Sin autenticación: cualquier proceso local puede consultar o enviar shutdown al servidor.
//@category cdmp
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.regex.Pattern;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class Cdmp_Query extends GhidraScript {
	// ---- límites (se aplican durante el procesamiento, no después)
	static final int MAX_REQUEST_BYTES = 4096;   // línea de petición
	static final int MAX_FIELD_CHARS = 512;      // cada campo
	static final int MAX_OUT_CHARS = 200_000;    // respuesta completa; al pasarlo se deja de trabajar
	static final int DISASM_MAX = 400, DISASM_DEFAULT = 40;
	static final int XREFS_MAX = 2000;
	static final int STR_MATCH_MAX = 200, STR_REFS_MAX = 30;
	static final int VT_SLOTS_MAX = 1024, CLASS_MAX = 40;
	static final int IMP_MAX = 50, IMP_USES_MAX = 500;
	static final int FUNC_LIST_MAX = 500, SYM_MAX = 200;
	static final int DECOMP_TIMEOUT_S = 60;
	static final int READ_TIMEOUT_MS = 30_000;
	static final Pattern HEX = Pattern.compile("(0[xX])?[0-9a-fA-F]{1,16}");
	static final Pattern COUNT = Pattern.compile("[0-9]{1,3}");
	static final Pattern INSTANCE = Pattern.compile("[0-9a-f]{32}");
	static final Map<String, int[]> ARITY = Map.ofEntries(
		Map.entry("decomp", new int[] { 1, 1 }), Map.entry("xrefs", new int[] { 1, 1 }),
		Map.entry("strings", new int[] { 1, 1 }), Map.entry("vtable", new int[] { 1, 1 }),
		Map.entry("class", new int[] { 1, 1 }), Map.entry("imports", new int[] { 1, 1 }),
		Map.entry("func", new int[] { 1, 1 }), Map.entry("symbols", new int[] { 1, 1 }),
		Map.entry("disasm", new int[] { 1, 2 }), Map.entry("ping", new int[] { 0, 0 }),
		Map.entry("shutdown", new int[] { 0, 0 }));

	/** Salida acotada: al superar el máximo marca truncado y lanza Limit para cortar el trabajo. */
	static final class Out {
		final StringBuilder sb = new StringBuilder();
		boolean truncated;
		Out a(Object o) {
			sb.append(o);
			if (sb.length() > MAX_OUT_CHARS) { sb.setLength(MAX_OUT_CHARS); truncated = true; throw new Limit(); }
			return this;
		}
		Out f(String fmt, Object... args) { return a(String.format(fmt, args)); }
		@Override public String toString() { return sb.toString(); }
	}

	static final class Limit extends RuntimeException {
		Limit() { super(null, null, false, false); }
	}

	static final class BadRequest extends Exception {
		BadRequest(String m) { super(m); }
	}

	private long BASE = 0x140000000L; // se toma de la base de imagen del programa
	private DecompInterface ifc;
	private String decompDir = "D:\\RE\\CrimsonDesert_2976\\exports\\decomp";
	private String instance = "";
	private List<Object[]> stringCache; // {Address, String}
	private List<Symbol> symbolCache;

	@Override
	public void run() throws Exception {
		BASE = currentProgram.getImageBase().getOffset();
		String req = null, out = null;
		Integer port = null;
		for (String s : getScriptArgs()) {
			if (s.startsWith("req=")) req = s.substring(4);
			else if (s.startsWith("out=")) out = s.substring(4);
			else if (s.startsWith("decompdir=")) decompDir = s.substring(10);
			else if (s.startsWith("serve=")) port = Integer.parseInt(s.substring(6));
			else if (s.startsWith("instance=")) {
				instance = s.substring(9);
				if (!INSTANCE.matcher(instance).matches()) throw new IllegalArgumentException("instance inválida");
			}
			else throw new IllegalArgumentException("argumento de script desconocido: " + s);
		}
		try {
			if (port != null) { serve(port); return; }
			if (req == null || out == null) throw new IllegalArgumentException("faltan req= y out=");
			byte[] b = Files.readAllBytes(Paths.get(req));
			String res = handle(b);
			Files.writeString(Paths.get(out), res, StandardCharsets.UTF_8);
		}
		finally {
			if (ifc != null) ifc.dispose();
		}
	}

	// ---------------------------------------------------------------- petición
	/** Valida la petición y la ejecuta; nunca lanza: devuelve el texto (o "ERROR: ..."). */
	private String handle(byte[] raw) {
		long t0 = System.nanoTime();
		List<String> f;
		try { f = parse(raw); }
		catch (BadRequest e) { return "ERROR: petición rechazada: " + e.getMessage() + "\n"; }
		Out o = new Out();
		try { dispatch(f, o); }
		catch (Limit e) { /* cortado */ }
		catch (BadRequest e) { return "ERROR: " + e.getMessage() + "\n"; }
		catch (Exception e) { return "ERROR: " + e + "\n"; }
		String res = o.toString();
		if (o.truncated) res += "\n... (respuesta cortada en " + MAX_OUT_CHARS + " caracteres; afina la consulta)\n";
		return res + String.format("# %s en %.2f s%n", f.get(0), (System.nanoTime() - t0) / 1e9);
	}

	static List<String> parse(byte[] raw) throws BadRequest {
		if (raw.length > MAX_REQUEST_BYTES) throw new BadRequest("más de " + MAX_REQUEST_BYTES + " bytes");
		int n = raw.length;
		while (n > 0 && (raw[n - 1] == '\n' || raw[n - 1] == '\r')) n--;
		String line;
		try {
			line = StandardCharsets.UTF_8.newDecoder().onMalformedInput(java.nio.charset.CodingErrorAction.REPORT)
				.decode(java.nio.ByteBuffer.wrap(raw, 0, n)).toString();
		}
		catch (java.nio.charset.CharacterCodingException e) { throw new BadRequest("UTF-8 inválido"); }
		if (line.startsWith("\uFEFF")) line = line.substring(1);
		String[] parts = line.split("\t", -1);
		List<String> f = new ArrayList<>(Arrays.asList(parts));
		String cmd = f.get(0);
		int[] ar = ARITY.get(cmd);
		if (ar == null) throw new BadRequest("comando desconocido: " + safe(cmd));
		int nargs = f.size() - 1;
		if (nargs < ar[0] || nargs > ar[1])
			throw new BadRequest(cmd + " admite " + (ar[0] == ar[1] ? ar[0] : ar[0] + "–" + ar[1]) + " argumento(s) y llegaron " + nargs);
		for (int i = 1; i < f.size(); i++) {
			String v = f.get(i);
			if (v.isEmpty()) throw new BadRequest("argumento " + i + " vacío");
			if (v.length() > MAX_FIELD_CHARS) throw new BadRequest("argumento " + i + " de más de " + MAX_FIELD_CHARS + " caracteres");
			for (int k = 0; k < v.length(); k++) {
				char c = v.charAt(k);
				if (c < 0x20 || c == 0x7F) throw new BadRequest("argumento " + i + " con caracteres de control");
			}
			if (!v.equals(v.strip())) throw new BadRequest("argumento " + i + " con espacios al principio o al final");
		}
		return f;
	}

	static String safe(String s) {
		String t = s.length() > 40 ? s.substring(0, 40) + "…" : s;
		return t.replaceAll("[\\p{Cntrl}]", "?");
	}

	private void dispatch(List<String> f, Out o) throws Exception {
		String cmd = f.get(0);
		String a1 = f.size() > 1 ? f.get(1) : null;
		switch (cmd) {
			case "decomp": cmdDecomp(resolve(a1), o); break;
			case "xrefs": cmdXrefs(resolve(a1), o); break;
			case "strings": cmdStrings(minLen(a1, 2), o); break;
			case "vtable": cmdVtable(resolve(a1), null, o); break;
			case "class": cmdClass(minLen(a1, 2), o); break;
			case "imports": cmdImports(minLen(a1, 2), o); break;
			case "func": cmdFunc(resolve(a1), o); break;
			case "symbols": cmdSymbols(minLen(a1, 2), o); break;
			case "disasm": cmdDisasm(resolve(a1), f.size() > 2 ? count(f.get(2)) : DISASM_DEFAULT, o); break;
			case "ping":
				o.a("pong\t" + currentProgram.getName() + "\t" + currentProgram.getExecutableSHA256() + "\t" +
					ProcessHandle.current().pid() + "\t" + instance + "\n");
				break;
			default: throw new BadRequest("comando no disponible en este modo: " + cmd);
		}
	}

	static String minLen(String s, int n) throws BadRequest {
		if (s.codePointCount(0, s.length()) < n) throw new BadRequest("escribe al menos " + n + " caracteres");
		return s;
	}

	static int count(String s) throws BadRequest {
		if (!COUNT.matcher(s).matches()) throw new BadRequest("el número de instrucciones debe ser un entero de 1 a " + DISASM_MAX);
		int n = Integer.parseInt(s);
		if (n < 1 || n > DISASM_MAX) throw new BadRequest("el número de instrucciones debe estar entre 1 y " + DISASM_MAX);
		return n;
	}

	// ---------------------------------------------------------------- servidor
	private void serve(int port) throws Exception {
		if (port < 1024 || port > 65535) throw new IllegalArgumentException("puerto fuera de rango");
		InetAddress lo = InetAddress.getByAddress("127.0.0.1", new byte[] { 127, 0, 0, 1 });
		try (ServerSocket ss = new ServerSocket(port, 8, lo)) {
			println("CDMP servidor escuchando en " + ss.getLocalSocketAddress() + " pid=" + ProcessHandle.current().pid() +
				" instancia=" + instance);
			buildStringCache();
			decomp(); // abre el decompilador ya
			while (!monitor.isCancelled()) {
				try (Socket s = ss.accept()) {
					s.setSoTimeout(READ_TIMEOUT_MS); // un cliente que no envía su línea no bloquea el servidor
					byte[] raw;
					try { raw = readLine(s.getInputStream()); }
					catch (BadRequest e) {
						write(s, "ERROR: petición rechazada: " + e.getMessage() + "\n");
						continue;
					}
					if (raw == null) continue;
					List<String> f = null;
					try { f = parse(raw); } catch (BadRequest e) { /* handle() da el error */ }
					if (f != null && f.get(0).equals("shutdown")) { write(s, "bye\t" + ProcessHandle.current().pid() + "\n"); break; }
					write(s, handle(raw));
				}
				catch (IOException e) { println("CDMP conexión fallida: " + e); }
			}
		}
		println("CDMP servidor parado");
	}

	/** Lee hasta '\n' sin pasar de MAX_REQUEST_BYTES; null si el cliente cierra sin enviar nada. */
	static byte[] readLine(InputStream in) throws IOException, BadRequest {
		ByteArrayOutputStream b = new ByteArrayOutputStream();
		int c;
		while ((c = in.read()) != -1) {
			if (c == '\n') return b.toByteArray();
			if (b.size() >= MAX_REQUEST_BYTES) throw new BadRequest("línea de más de " + MAX_REQUEST_BYTES + " bytes");
			b.write(c);
		}
		return b.size() == 0 ? null : b.toByteArray();
	}

	static void write(Socket s, String txt) throws IOException {
		OutputStream os = s.getOutputStream();
		os.write(txt.getBytes(StandardCharsets.UTF_8));
		os.flush();
	}

	// ---------------------------------------------------------------- utilidades
	private String rva(Address a) {
		if (a == null) return "?";
		if (!a.isMemoryAddress()) return a.toString();
		return String.format("0x%X", a.getOffset() - BASE);
	}

	private String block(Address a) {
		MemoryBlock b = currentProgram.getMemory().getBlock(a);
		return b == null ? "?" : b.getName();
	}

	private String where(Address a) {
		Function f = getFunctionContaining(a);
		if (f == null) return "(sin función)";
		long off = a.subtract(f.getEntryPoint());
		return f.getName(true) + " [" + rva(f.getEntryPoint()) + "]" + (off != 0 ? String.format("+0x%X", off) : "");
	}

	/** RVA/VA en hex (dentro de un bloque de memoria), o un nombre de símbolo/función (también cdmp_). */
	private Address resolve(String s) throws BadRequest {
		String h = s.toLowerCase().startsWith("0x") ? s.substring(2) : s;
		if (HEX.matcher(s).matches() && (s.toLowerCase().startsWith("0x") || h.length() >= 5)) {
			long v;
			try { v = Long.parseUnsignedLong(h, 16); } catch (NumberFormatException e) { throw new BadRequest("número inválido"); }
			if (Long.compareUnsigned(v, BASE) < 0) v += BASE;
			Address a;
			try { a = toAddr(v); } catch (Exception e) { throw new BadRequest("dirección fuera del espacio: " + s); }
			if (a == null || currentProgram.getMemory().getBlock(a) == null)
				throw new BadRequest("la dirección " + s + " no está en ningún bloque de memoria");
			return a;
		}
		SymbolTable st = currentProgram.getSymbolTable();
		String[] cands = { s, "cdmp_" + s, "cdmp_" + s.replace("::", "__"), s.replace("::", "__") };
		for (String c : cands)
			for (Symbol sym : st.getSymbols(c)) if (sym.getAddress().isMemoryAddress()) return sym.getAddress();
		for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
			String fn = f.getName(true);
			if (fn.equals(s) || fn.equalsIgnoreCase(s)) return f.getEntryPoint();
		}
		throw new BadRequest("no encuentro la dirección o el nombre: " + safe(s));
	}

	private DecompInterface decomp() {
		if (ifc == null) {
			ifc = new DecompInterface();
			DecompileOptions o = new DecompileOptions();
			o.grabFromProgram(currentProgram);
			ifc.setOptions(o);
			ifc.toggleCCode(true);
			ifc.toggleSyntaxTree(true);
			ifc.setSimplificationStyle("decompile");
			if (!ifc.openProgram(currentProgram)) throw new RuntimeException("decompilador: " + ifc.getLastMessage());
		}
		return ifc;
	}

	private Iterable<Reference> refsTo(Address a) {
		return currentProgram.getReferenceManager().getReferencesTo(a);
	}

	// ---------------------------------------------------------------- decomp
	private void cmdDecomp(Address a, Out o) throws Exception {
		Function f = getFunctionContaining(a);
		if (f == null) { o.a("No hay función que contenga " + rva(a) + " (bloque " + block(a) + ")\n"); return; }
		DecompileResults r = decomp().decompileFunction(f, DECOMP_TIMEOUT_S, monitor);
		StringBuilder hdr = new StringBuilder();
		hdr.append("// ").append(f.getName(true)).append("  RVA ").append(rva(f.getEntryPoint()))
			.append("  VA ").append(f.getEntryPoint()).append("  bloque ").append(block(f.getEntryPoint()))
			.append("  tamaño 0x").append(Long.toHexString(f.getBody().getNumAddresses())).append('\n');
		if (!a.equals(f.getEntryPoint())) hdr.append("// (pedido ").append(rva(a)).append(", dentro de la función)\n");
		hdr.append("// ").append(f.getPrototypeString(false, false)).append('\n');
		Function th = f.isThunk() ? f.getThunkedFunction(true) : null;
		if (th != null) hdr.append("// thunk de ").append(th.getName(true)).append(' ').append(rva(th.getEntryPoint())).append('\n');
		String c;
		if (r != null && r.decompileCompleted() && r.getDecompiledFunction() != null) c = r.getDecompiledFunction().getC();
		else c = "/* decompilación fallida: " + (r == null ? "?" : r.getErrorMessage()) + " */\n";
		String text = hdr + c;
		if (text.length() > MAX_OUT_CHARS - 200) text = text.substring(0, MAX_OUT_CHARS - 200) + "\n/* ... cortado */\n";
		// El nombre del fichero sale de la RVA numérica, nunca del texto pedido.
		Path p = Paths.get(decompDir, String.format("0x%x.c", f.getEntryPoint().getOffset() - BASE));
		Files.createDirectories(p.getParent());
		Files.writeString(p, text, StandardCharsets.UTF_8);
		o.a(text).a("// guardado en " + p + "\n");
	}

	// ---------------------------------------------------------------- symbols
	private void cmdSymbols(String arg, Out o) {
		String q = arg.toLowerCase();
		if (symbolCache == null) {
			symbolCache = new ArrayList<>();
			for (Symbol s : currentProgram.getSymbolTable().getAllSymbols(false))
				if (s.getSource() != SourceType.DEFAULT && s.getAddress().isMemoryAddress()) symbolCache.add(s);
		}
		int n = 0;
		for (Symbol s : symbolCache) {
			String full = s.getName(true);
			if (!full.toLowerCase().contains(q)) continue;
			if (++n > SYM_MAX) { o.a("... (más de " + SYM_MAX + "; afina la búsqueda)\n"); break; }
			o.f("  %-12s %-9s %-8s %s%n", rva(s.getAddress()), s.getSymbolType(), block(s.getAddress()), full);
		}
		o.a(Math.min(n, SYM_MAX) + " símbolos (de " + symbolCache.size() + " con nombre)\n");
	}

	// ---------------------------------------------------------------- disasm
	private void cmdDisasm(Address a, int n, Out o) throws Exception {
		o.a("Desensamblado desde " + rva(a) + " (" + block(a) + ") en " + where(a) + "\n");
		Instruction ins = getInstructionAt(a);
		if (ins == null) {
			o.a("  (no hay instrucción que empiece en " + rva(a) + "; se muestra desde la siguiente)\n");
			ins = getInstructionAfter(a);
		}
		for (int i = 0; i < n && ins != null; i++, ins = ins.getNext()) {
			Address ia = ins.getAddress();
			for (Symbol s : currentProgram.getSymbolTable().getSymbols(ia))
				if (s.getSource() != SourceType.DEFAULT) o.a("                " + s.getName(true) + ":\n");
			StringBuilder hx = new StringBuilder();
			for (byte x : ins.getBytes()) hx.append(String.format("%02x ", x));
			StringBuilder refs = new StringBuilder();
			for (Reference r : ins.getReferencesFrom()) {
				Address t = r.getToAddress();
				Symbol ts = getSymbolAt(t);
				if (t.isMemoryAddress()) refs.append("  -> ").append(rva(t)).append(ts != null ? " " + ts.getName(true) : "");
				else if (ts != null) refs.append("  -> ").append(ts.getName(true));
			}
			o.f("  %-10s %-33s %s%s%n", rva(ia), hx.toString().trim(), ins, refs);
		}
	}

	// ---------------------------------------------------------------- func
	private void cmdFunc(Address a, Out o) throws Exception {
		Function f = getFunctionContaining(a);
		if (f == null) { o.a("No hay función en " + rva(a) + " (bloque " + block(a) + ")\n"); return; }
		o.a(f.getName(true) + "  RVA " + rva(f.getEntryPoint()) + "  tamaño 0x" + Long.toHexString(f.getBody().getNumAddresses()) +
			"  bloque " + block(f.getEntryPoint()) + "\n");
		o.a("firma: " + f.getPrototypeString(false, false) + "\n");
		for (Symbol s : currentProgram.getSymbolTable().getSymbols(f.getEntryPoint()))
			o.a("símbolo: " + s.getName(true) + " (" + s.getSource() + ")\n");
		String pc = f.getComment();
		if (pc != null) o.a("comentario: " + pc.replace("\n", " | ") + "\n");
		Set<Function> callers = f.getCallingFunctions(monitor), callees = f.getCalledFunctions(monitor);
		list(o, "llamadores", callers);
		list(o, "llamados", callees);
	}

	private void list(Out o, String title, Set<Function> fs) {
		o.a(title + " (" + fs.size() + "):\n");
		int i = 0;
		for (Function c : fs) {
			if (++i > FUNC_LIST_MAX) { o.a("  ... (" + (fs.size() - FUNC_LIST_MAX) + " más)\n"); break; }
			o.a("  " + rva(c.getEntryPoint()) + "  " + c.getName(true) + "\n");
		}
	}

	// ---------------------------------------------------------------- xrefs
	private void cmdXrefs(Address a, Out o) {
		Function f = getFunctionAt(a);
		Symbol ps = getSymbolAt(a);
		o.a("Referencias a " + rva(a) + " (" + block(a) + ") " + (ps != null ? ps.getName(true) : "") + "\n");
		List<Address> targets = new ArrayList<>();
		targets.add(a);
		if (f != null) {
			Address[] th = f.getFunctionThunkAddresses(true);
			if (th != null) targets.addAll(Arrays.asList(th));
		}
		int n = 0;
		outer:
		for (Address t : targets) {
			if (!t.equals(a)) o.a("-- vía thunk " + rva(t) + "\n");
			for (Reference r : refsTo(t)) {
				if (n >= XREFS_MAX) { o.a("... (más de " + XREFS_MAX + " referencias; se paró aquí)\n"); break outer; }
				Address from = r.getFromAddress();
				Instruction ins = getInstructionAt(from);
				o.f("  %-12s %-18s %s   %s%n", rva(from), r.getReferenceType(), where(from), ins != null ? ins.toString() : "");
				n++;
			}
		}
		o.a(n + " referencias" + (n >= XREFS_MAX ? " (como mínimo)" : "") + "\n");
	}

	// ---------------------------------------------------------------- strings
	private void buildStringCache() {
		if (stringCache != null) return;
		stringCache = new ArrayList<>();
		DataIterator it = currentProgram.getListing().getDefinedData(true);
		while (it.hasNext()) {
			Data d = it.next();
			if (!StringDataInstance.isString(d)) continue;
			String v = StringDataInstance.getStringDataInstance(d).getStringValue();
			if (v != null) stringCache.add(new Object[] { d.getAddress(), v });
		}
	}

	private void cmdStrings(String arg, Out o) {
		buildStringCache();
		String q = arg.toLowerCase();
		int n = 0;
		for (Object[] e : stringCache) {
			String v = (String) e[1];
			if (!v.toLowerCase().contains(q)) continue;
			Address a = (Address) e[0];
			if (++n > STR_MATCH_MAX) { o.a("... (más de " + STR_MATCH_MAX + " coincidencias; afina la búsqueda)\n"); break; }
			o.a(rva(a) + "  \"" + v.replace("\n", "\\n").replace("\r", "\\r") + "\"\n");
			int k = 0;
			for (Reference r : refsTo(a)) {
				if (++k > STR_REFS_MAX) { o.a("      ...\n"); break; }
				o.a("      <- " + rva(r.getFromAddress()) + "  " + where(r.getFromAddress()) + "\n");
			}
		}
		o.a(Math.min(n, STR_MATCH_MAX) + " cadenas (de " + stringCache.size() + " definidas)\n");
	}

	// ---------------------------------------------------------------- vtables
	private boolean isCodePtr(Address t) {
		MemoryBlock b = currentProgram.getMemory().getBlock(t);
		if (b == null || !b.isInitialized()) return false;
		String n = b.getName();
		return b.isExecute() || n.equals(".xtls") || n.equals(".text");
	}

	private void cmdVtable(Address a, String title, Out o) {
		Symbol s0 = getSymbolAt(a);
		o.a("vtable " + rva(a) + " " + (title != null ? title : s0 != null ? s0.getName(true) : "") + "\n");
		Memory m = currentProgram.getMemory();
		int i;
		for (i = 0; i < VT_SLOTS_MAX; i++) {
			Address slot = a.add(8L * i);
			if (i > 0) {
				Symbol s = getSymbolAt(slot);
				if (s != null && s.getSource() != SourceType.DEFAULT && !s.getName().startsWith("cdmp_")) break;
			}
			long v;
			try { v = m.getLong(slot); } catch (MemoryAccessException e) { break; }
			Address t;
			try { t = toAddr(v); } catch (Exception e) { break; }
			if (t == null || !isCodePtr(t)) break;
			Function f = getFunctionAt(t);
			o.f("  [%3d] +0x%03X  %-12s %-6s %s%n", i, 8 * i, rva(t), block(t), f != null ? f.getName(true) : "(sin función)");
		}
		if (i >= VT_SLOTS_MAX) o.a("  ... (se paró en " + VT_SLOTS_MAX + " entradas)\n");
	}

	private void cmdClass(String name, Out o) {
		String q = name.toLowerCase();
		List<Symbol> exact = new ArrayList<>(), part = new ArrayList<>();
		for (Symbol s : currentProgram.getSymbolTable().getSymbols("vftable")) {
			String nsl = s.getParentNamespace().getName(true).toLowerCase();
			if (nsl.equals(q) || nsl.endsWith("::" + q)) exact.add(s);
			else if (nsl.contains(q)) part.add(s);
		}
		List<Symbol> use = exact.isEmpty() ? part : exact;
		int n = 0;
		for (Symbol s : use) {
			if (++n > CLASS_MAX) { o.a("... (más de " + CLASS_MAX + " vftables)\n"); break; }
			cmdVtable(s.getAddress(), s.getName(true), o);
			o.a("\n");
		}
		if (use.isEmpty()) o.a("Ninguna vftable cuya clase contenga '" + name + "'\n");
	}

	// ---------------------------------------------------------------- imports
	private void cmdImports(String name, Out o) {
		String q = name.toLowerCase();
		int n = 0;
		for (Symbol s : currentProgram.getSymbolTable().getExternalSymbols()) {
			if (!s.getName().toLowerCase().contains(q)) continue;
			if (++n > IMP_MAX) { o.a("... (más de " + IMP_MAX + " importaciones)\n"); break; }
			o.a(s.getName(true) + "\n");
			Set<Address> seen = new HashSet<>();
			List<Address> via = new ArrayList<>();
			for (Reference r : s.getReferences()) via.add(r.getFromAddress());
			if (s.getObject() instanceof Function ef) {
				Address[] th = ef.getFunctionThunkAddresses(true);
				if (th != null) via.addAll(Arrays.asList(th));
			}
			int calls = 0;
			outer:
			for (Address v : via) {
				if (!v.isMemoryAddress()) continue;
				Instruction ins = getInstructionAt(v);
				if (ins != null) {
					if (seen.add(v)) {
						if (calls >= IMP_USES_MAX) break;
						o.f("  %-12s %s   %s%n", rva(v), where(v), ins);
						calls++;
					}
					continue;
				}
				o.a("  puntero/thunk " + rva(v) + " (" + block(v) + ")\n");
				for (Reference r : refsTo(v)) {
					Address from = r.getFromAddress();
					if (!seen.add(from)) continue;
					if (calls >= IMP_USES_MAX) { o.a("    ... (más de " + IMP_USES_MAX + " usos)\n"); break outer; }
					Instruction i2 = getInstructionAt(from);
					o.f("    %-12s %-14s %s   %s%n", rva(from), r.getReferenceType(), where(from), i2 != null ? i2.toString() : "");
					calls++;
				}
			}
			o.a("  " + calls + " usos\n");
		}
		if (n == 0) o.a("Ninguna importación contiene '" + name + "'\n");
	}
}
