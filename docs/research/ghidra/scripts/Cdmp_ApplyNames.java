// cdmp: aplica nombres, etiquetas y comentarios conocidos por el equipo (todo con prefijo cdmp_).
// Build 1.0.0.2976 (PE ts 0x6AB28F00). Las RVA de las fuentes se suman a la base 0x140000000.
//
// Argumentos:  names=<csv rva,kind,name,comment,source>  troc=<troc_messages_2976.csv>  report=<csv salida>
// Idempotente: si el nombre cdmp_ ya está en esa dirección no lo repite; los comentarios no se duplican.
// No crea funciones dentro de .xtls ni a mitad de otra función: ahí solo pone etiqueta y comentario.
//@category cdmp
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;

public class Cdmp_ApplyNames extends GhidraScript {
	private static final long BASE = 0x140000000L;
	private PrintWriter rep;
	private final Map<String, Integer> stats = new TreeMap<>();

	@Override
	public void run() throws Exception {
		String names = null, troc = null, report = "D:\\RE\\CrimsonDesert_2976\\exports\\cdmp_names_report.csv";
		for (String s : getScriptArgs()) {
			if (s.startsWith("names=")) names = s.substring(6);
			else if (s.startsWith("troc=")) troc = s.substring(5);
			else if (s.startsWith("report=")) report = s.substring(7);
		}
		rep = new PrintWriter(Files.newBufferedWriter(Paths.get(report), StandardCharsets.UTF_8));
		rep.println("rva,kind,name,status,detail,source");
		if (names != null) applyRepo(names);
		if (troc != null) applyTroc(troc);
		rep.close();
		int applied = 0;
		for (Map.Entry<String, Integer> e : stats.entrySet()) {
			println("CDMP estado " + e.getKey() + " = " + e.getValue());
			if (!e.getKey().startsWith("x_")) applied += e.getValue();
		}
		println("CDMP filas que encajaron (estados sin x_) = " + applied + "  (informe: " + report + ")");
		int nsym = 0, nfun = 0;
		for (Symbol s : currentProgram.getSymbolTable().getAllSymbols(false)) {
			if (!s.getName().startsWith("cdmp_")) continue;
			nsym++;
			if (s.getSymbolType() == SymbolType.FUNCTION) nfun++;
		}
		println("CDMP símbolos cdmp_ en el programa = " + nsym + " (de ellos nombres de función = " + nfun + ")");
	}

	// ------------------------------------------------------------------ CSV
	static List<List<String>> readCsv(String path) throws IOException {
		List<List<String>> rows = new ArrayList<>();
		String all = Files.readString(Paths.get(path), StandardCharsets.UTF_8);
		if (all.startsWith("\uFEFF")) all = all.substring(1);
		List<String> row = new ArrayList<>();
		StringBuilder f = new StringBuilder();
		boolean q = false;
		for (int i = 0; i < all.length(); i++) {
			char c = all.charAt(i);
			if (q) {
				if (c == '"') {
					if (i + 1 < all.length() && all.charAt(i + 1) == '"') { f.append('"'); i++; }
					else q = false;
				}
				else f.append(c);
			}
			else if (c == '"') q = true;
			else if (c == ',') { row.add(f.toString()); f.setLength(0); }
			else if (c == '\n' || c == '\r') {
				if (c == '\r' && i + 1 < all.length() && all.charAt(i + 1) == '\n') i++;
				row.add(f.toString()); f.setLength(0);
				if (!(row.size() == 1 && row.get(0).isEmpty())) rows.add(row);
				row = new ArrayList<>();
			}
			else f.append(c);
		}
		if (f.length() > 0 || !row.isEmpty()) { row.add(f.toString()); rows.add(row); }
		return rows;
	}

	static String csv(String s) {
		if (s == null) return "";
		if (s.contains(",") || s.contains("\"") || s.contains("\n")) return "\"" + s.replace("\"", "\"\"") + "\"";
		return s;
	}

	private void report(long rva, String kind, String name, String status, String detail, String src) {
		stats.merge(status, 1, Integer::sum);
		rep.println(String.format("0x%X", rva) + "," + csv(kind) + "," + csv(name) + "," + status + "," + csv(detail) + "," + csv(src));
	}

	static String sanitize(String n) {
		n = n.trim().replace("::", "__");
		StringBuilder b = new StringBuilder();
		for (char c : n.toCharArray()) b.append(Character.isLetterOrDigit(c) || c == '_' ? c : '_');
		return b.toString();
	}

	static long parseHex(String s) {
		s = s.trim().toLowerCase();
		if (s.startsWith("0x")) s = s.substring(2);
		return Long.parseUnsignedLong(s, 16);
	}

	// ------------------------------------------------------------------ primitivas
	private boolean hasSymbol(Address a, String name) {
		for (Symbol s : currentProgram.getSymbolTable().getSymbols(a)) if (s.getName().equals(name)) return true;
		return false;
	}

	/** Nombre único en el espacio global: si ya existe en otra dirección, añade _<rva>. */
	private String uniqueName(Address a, String name) {
		for (Symbol s : currentProgram.getSymbolTable().getGlobalSymbols(name))
			if (!s.getAddress().equals(a)) return name + String.format("_%X", a.getOffset() - BASE);
		return name;
	}

	private void addComment(Address a, CommentType type, String text) {
		Listing l = currentProgram.getListing();
		String old = l.getComment(type, a);
		if (old != null && old.contains(text)) return;
		l.setComment(a, type, old == null ? text : old + "\n" + text);
	}

	private boolean inBlock(Address a, String name) {
		MemoryBlock b = currentProgram.getMemory().getBlock(a);
		return b != null && b.getName().equals(name);
	}

	/**
	 * Nombra una función (o deja etiqueta si no encaja). Devuelve el estado.
	 * Estados que empiezan por "x_" son no aplicados.
	 */
	private String nameFunction(Address a, String sym, String comment, StringBuilder detail, boolean allowCreate) throws Exception {
		Function f = getFunctionAt(a);
		if (f == null && allowCreate && !inBlock(a, ".xtls") && getFunctionContaining(a) == null &&
			currentProgram.getMemory().getBlock(a).isExecute()) {
			if (getInstructionAt(a) == null) disassemble(a);
			if (getInstructionAt(a) != null) {
				f = createFunction(a, null);
				if (f != null) detail.append("función creada; ");
			}
		}
		if (f != null) {
			if (f.getName().equals(sym) || hasSymbol(a, sym)) { addComment(a, CommentType.PLATE, comment); return "ya_estaba"; }
			SourceType st = f.getSymbol().getSource();
			// Un nombre con espacio de nombres (demanglado de una exportación, p. ej. AK::TempAlloc::…) se
			// respeta: suele ser un stub compartido por plegado de código idéntico.
			boolean global = f.getParentNamespace().isGlobal();
			if (st == SourceType.DEFAULT || (st == SourceType.ANALYSIS && global)) {
				String old = f.getName();
				f.setName(uniqueName(a, sym), SourceType.USER_DEFINED);
				detail.append("antes ").append(old);
				addComment(a, CommentType.PLATE, comment);
				return "funcion_renombrada";
			}
			createLabel(a, uniqueName(a, sym), false, SourceType.USER_DEFINED);
			detail.append("la función ya se llamaba ").append(f.getName(true)).append(" (se deja; cdmp_ como etiqueta)");
			addComment(a, CommentType.PLATE, comment);
			return "etiqueta_en_funcion_con_nombre";
		}
		Function c = getFunctionContaining(a);
		if (!hasSymbol(a, sym)) createLabel(a, uniqueName(a, sym), false, SourceType.USER_DEFINED);
		addComment(a, CommentType.PRE, comment);
		if (inBlock(a, ".xtls")) { detail.append("dentro de .xtls (virtualizado)"); return "x_func_en_xtls_etiqueta"; }
		if (c != null) {
			detail.append(String.format("no es inicio de función: dentro de %s [0x%X]+0x%X", c.getName(true),
				c.getEntryPoint().getOffset() - BASE, a.subtract(c.getEntryPoint())));
			return "x_func_no_es_inicio_etiqueta";
		}
		detail.append("sin función ni instrucción en esa dirección");
		return "x_func_sin_funcion_etiqueta";
	}

	private String label(Address a, String sym, String comment, CommentType ct) throws Exception {
		if (hasSymbol(a, sym)) { addComment(a, ct, comment); return "ya_estaba"; }
		createLabel(a, uniqueName(a, sym), false, SourceType.USER_DEFINED);
		addComment(a, ct, comment);
		return "etiqueta";
	}

	// ------------------------------------------------------------------ repo
	private void applyRepo(String path) throws Exception {
		List<List<String>> rows = readCsv(path);
		Map<String, Integer> h = header(rows.get(0));
		for (List<String> r : rows.subList(1, rows.size())) {
			String rvaS = get(r, h, "rva"), kind = get(r, h, "kind").trim().toLowerCase(), name = get(r, h, "name");
			String com = get(r, h, "comment"), src = get(r, h, "source");
			long rva;
			try { rva = parseHex(rvaS); } catch (Exception e) { report(0, kind, name, "x_rva_invalida", rvaS, src); continue; }
			if (rva >= BASE) rva -= BASE;
			Address a = toAddr(BASE + rva);
			if (currentProgram.getMemory().getBlock(a) == null) { report(rva, kind, name, "x_fuera_de_memoria", "", src); continue; }
			String sym = "cdmp_" + sanitize(name);
			String comment = "cdmp: " + name + " — " + com + " [" + src + "]";
			StringBuilder d = new StringBuilder();
			String st;
			try {
				switch (kind) {
					case "func": st = nameFunction(a, sym, comment, d, true); break;
					case "code":
						st = label(a, sym, comment, CommentType.PRE);
						if (getInstructionAt(a) == null) { d.append("no hay instrucción en esa dirección"); st = "x_code_sin_instruccion_etiqueta"; }
						else st = "etiqueta_codigo";
						break;
					case "vtable": st = label(a, sym, comment, CommentType.PLATE); if (st.equals("etiqueta")) st = "etiqueta_vtable"; break;
					case "string": st = label(a, sym, comment, CommentType.PLATE); if (st.equals("etiqueta")) st = "etiqueta_cadena"; break;
					default: st = label(a, sym, comment, CommentType.PLATE); if (st.equals("etiqueta")) st = "etiqueta_datos"; break;
				}
			}
			catch (Exception e) { st = "x_error"; d.append(e.toString()); }
			d.insert(0, "bloque " + currentProgram.getMemory().getBlock(a).getName() + "; ");
			report(rva, kind, sym, st, d.toString(), src);
		}
	}

	private static Map<String, Integer> header(List<String> h) {
		Map<String, Integer> m = new HashMap<>();
		for (int i = 0; i < h.size(); i++) m.put(h.get(i).trim().toLowerCase(), i);
		return m;
	}

	private static String get(List<String> r, Map<String, Integer> h, String k) {
		Integer i = h.get(k);
		return i == null || i >= r.size() ? "" : r.get(i);
	}

	// ------------------------------------------------------------------ Troc
	private void applyTroc(String path) throws Exception {
		List<List<String>> rows = readCsv(path);
		Map<String, Integer> h = header(rows.get(0));
		List<List<String>> body = rows.subList(1, rows.size());
		// funciones compartidas por varios mensajes
		Map<String, List<String>> byHandler = new HashMap<>(), byRecv = new HashMap<>(), byReg = new HashMap<>();
		for (List<String> r : body) {
			String n = get(r, h, "name");
			byHandler.computeIfAbsent(get(r, h, "handler_slot2").toLowerCase(), k -> new ArrayList<>()).add(n);
			byRecv.computeIfAbsent(get(r, h, "on_receive_slot1").toLowerCase(), k -> new ArrayList<>()).add(n);
			byReg.computeIfAbsent(get(r, h, "register_fn").toLowerCase(), k -> new ArrayList<>()).add(n);
		}
		String src = "docs/research/data/troc_messages_2976.csv";
		for (List<String> r : body) {
			String n = get(r, h, "name"), id = get(r, h, "id"), kind = get(r, h, "kind"), dir = get(r, h, "dir");
			String dom = get(r, h, "domain");
			String desc = "Troc " + n + " opcode " + id + " kind " + kind + " (" + dir + ", " + dom + ")";
			String vt = get(r, h, "vtable"), inst = get(r, h, "static_instance"), hs = get(r, h, "handler_slot2");
			trocLabel(vt, "cdmp_troc_" + sanitize(n) + "_vtable", "cdmp: vtable de " + desc + " [" + src + "]", "troc_vtable", n);
			trocLabel(inst, "cdmp_troc_" + sanitize(n) + "_inst", "cdmp: instancia estática (0x90 B) de " + desc + " [" + src + "]", "troc_instancia", n);
			if (!hs.isBlank()) {
				List<String> users = byHandler.get(hs.toLowerCase());
				if (users.size() == 1) trocFunc(hs, "cdmp_troc_" + sanitize(n) + "_Handler", "cdmp: manejador (vtable slot 2) de " + desc + " [" + src + "]", n);
			}
		}
		// manejadores compartidos, OnReceive (slot 1) y funciones de registro: una vez cada uno
		for (Map.Entry<String, List<String>> e : byHandler.entrySet())
			if (e.getValue().size() > 1) trocShared(e.getKey(), "cdmp_troc_SharedHandler", "manejador (slot 2) compartido", e.getValue());
		for (Map.Entry<String, List<String>> e : byRecv.entrySet())
			trocShared(e.getKey(), "cdmp_troc_OnReceive", "OnReceive (vtable slot 1)", e.getValue());
		for (Map.Entry<String, List<String>> e : byReg.entrySet())
			trocShared(e.getKey(), "cdmp_troc_Register", "función de registro", e.getValue());
	}

	private Address trocAddr(String hex) {
		if (hex == null || hex.isBlank()) return null;
		long v = parseHex(hex);
		if (v >= BASE) v -= BASE;
		Address a = toAddr(BASE + v);
		return currentProgram.getMemory().getBlock(a) == null ? null : a;
	}

	private void trocLabel(String hex, String sym, String comment, String what, String msg) throws Exception {
		Address a = trocAddr(hex);
		if (a == null) { if (hex != null && !hex.isBlank()) report(parseHex(hex), what, sym, "x_fuera_de_memoria", msg, "troc"); return; }
		String st = label(a, sym, comment, CommentType.PLATE);
		report(a.getOffset() - BASE, what, sym, st.equals("etiqueta") ? "etiqueta_" + what : st, msg, "troc");
	}

	private void trocFunc(String hex, String sym, String comment, String msg) throws Exception {
		Address a = trocAddr(hex);
		if (a == null) { report(parseHex(hex), "troc_handler", sym, "x_fuera_de_memoria", msg, "troc"); return; }
		StringBuilder d = new StringBuilder("bloque " + currentProgram.getMemory().getBlock(a).getName() + "; ");
		String st = nameFunction(a, sym, comment, d, true);
		report(a.getOffset() - BASE, "troc_handler", sym, st, d.toString(), "troc");
	}

	private void trocShared(String hex, String base, String what, List<String> users) throws Exception {
		Address a = trocAddr(hex);
		if (a == null) return;
		String sym = base + String.format("_%X", a.getOffset() - BASE);
		StringBuilder list = new StringBuilder();
		for (int i = 0; i < users.size() && i < 25; i++) list.append(i > 0 ? ", " : "").append(users.get(i));
		if (users.size() > 25) list.append(", … (").append(users.size()).append(" en total)");
		String comment = "cdmp: Troc " + what + " de " + users.size() + " mensajes: " + list + " [docs/research/data/troc_messages_2976.csv]";
		StringBuilder d = new StringBuilder("bloque " + currentProgram.getMemory().getBlock(a).getName() + "; " + users.size() + " mensajes; ");
		String st = nameFunction(a, sym, comment, d, true);
		report(a.getOffset() - BASE, "troc_shared", sym, st, d.toString(), "troc");
	}
}
