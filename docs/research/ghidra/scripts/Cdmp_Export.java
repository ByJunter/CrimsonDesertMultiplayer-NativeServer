// cdmp: exportaciones únicas del proyecto analizado (usar con -noanalysis -readOnly).
//   functions.csv  rva,size,name,block,thunk,source
//   strings.csv    rva,block,type,length,nrefs,value,refs   (refs: rva@función, máx. 20)
//   rtti_classes.csv  class,type_descriptor_rva,vtable_rva,nslots,slots
//   sections.csv   name,start_rva,end_rva,size,perm_ghidra,perm_pe,initialized
//   imports.csv    library,name,ptr_rva,uses,callers   (callers: rva@función, máx. 30)
//   cdmp_symbols.csv  rva,type,primary,name,block   (todos los símbolos cuyo nombre empieza por cdmp_)
// Argumento opcional: dir=<carpeta de salida> (por defecto D:\RE\CrimsonDesert_2976\exports).
//@category cdmp
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.framework.options.Options;
import ghidra.program.model.address.*;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class Cdmp_Export extends GhidraScript {
	private long BASE;

	@Override
	public void run() throws Exception {
		BASE = currentProgram.getImageBase().getOffset();
		String dir = "D:\\RE\\CrimsonDesert_2976\\exports";
		for (String s : getScriptArgs()) if (s.startsWith("dir=")) dir = s.substring(4);
		Files.createDirectories(Paths.get(dir));
		long t;
		t = System.nanoTime(); sections(Paths.get(dir, "sections.csv")); took("sections", t);
		t = System.nanoTime(); functions(Paths.get(dir, "functions.csv")); took("functions", t);
		t = System.nanoTime(); strings(Paths.get(dir, "strings.csv")); took("strings", t);
		t = System.nanoTime(); rtti(Paths.get(dir, "rtti_classes.csv")); took("rtti", t);
		t = System.nanoTime(); imports(Paths.get(dir, "imports.csv")); took("imports", t);
		t = System.nanoTime(); cdmpSymbols(Paths.get(dir, "cdmp_symbols.csv")); took("cdmp_symbols", t);
	}

	private void took(String what, long t0) { println(String.format("CDMP export %s en %.1f s", what, (System.nanoTime() - t0) / 1e9)); }

	private String rva(Address a) { return a.isMemoryAddress() ? String.format("0x%X", a.getOffset() - BASE) : a.toString(); }

	private static String csv(String s) {
		if (s == null) return "";
		s = s.replace("\r", "\\r").replace("\n", "\\n");
		if (s.contains(",") || s.contains("\"") || s.startsWith(" ") || s.endsWith(" ")) return "\"" + s.replace("\"", "\"\"") + "\"";
		return s;
	}

	private PrintWriter open(Path p) throws IOException {
		return new PrintWriter(new BufferedWriter(new OutputStreamWriter(Files.newOutputStream(p), StandardCharsets.UTF_8), 1 << 20));
	}

	private String at(Address a) {
		Function f = getFunctionContaining(a);
		return rva(a) + "@" + (f == null ? "" : f.getName(true));
	}

	// ----------------------------------------------------------------
	/** Permisos de una sección según la tabla de secciones del PE (bloque Headers), p. ej. "RWX". */
	static String pePerm(ghidra.program.model.listing.Program p, String name) throws Exception {
		ghidra.program.model.mem.Memory m = p.getMemory();
		ghidra.program.model.address.Address h = p.getImageBase();
		int lfanew = m.getInt(h.add(0x3C));
		int nsec = m.getShort(h.add(lfanew + 6)) & 0xFFFF;
		int optsz = m.getShort(h.add(lfanew + 20)) & 0xFFFF;
		long so = lfanew + 24 + optsz;
		for (int i = 0; i < nsec; i++, so += 40) {
			byte[] nb = new byte[8];
			m.getBytes(h.add(so), nb);
			String n = new String(nb, java.nio.charset.StandardCharsets.ISO_8859_1).replace("\0", "");
			if (!n.equals(name)) continue;
			int ch = m.getInt(h.add(so + 36));
			return ((ch & 0x40000000) != 0 ? "R" : "-") + ((ch & 0x80000000) != 0 ? "W" : "-") + ((ch & 0x20000000) != 0 ? "X" : "-");
		}
		return null;
	}

	private void sections(Path p) throws IOException {
		try (PrintWriter w = open(p)) {
			w.println("name,start_rva,end_rva,size,perm_ghidra,perm_pe,initialized");
			for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
				String perm = (b.isRead() ? "R" : "-") + (b.isWrite() ? "W" : "-") + (b.isExecute() ? "X" : "-");
				String pe;
				try { pe = pePerm(currentProgram, b.getName()); } catch (Exception e) { pe = null; }
				if (pe == null) pe = "";
				w.println(csv(b.getName()) + "," + rva(b.getStart()) + "," + rva(b.getEnd()) + "," +
					String.format("0x%X", b.getSize()) + "," + perm + "," + pe + "," + b.isInitialized());
			}
		}
	}

	private void functions(Path p) throws IOException {
		int n = 0;
		Map<String, Integer> perBlock = new TreeMap<>();
		try (PrintWriter w = open(p)) {
			w.println("rva,size,name,block,thunk,source");
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				MemoryBlock b = currentProgram.getMemory().getBlock(f.getEntryPoint());
				String bn = b == null ? "?" : b.getName();
				perBlock.merge(bn, 1, Integer::sum);
				w.println(rva(f.getEntryPoint()) + "," + String.format("0x%X", f.getBody().getNumAddresses()) + "," +
					csv(f.getName(true)) + "," + bn + "," + (f.isThunk() ? 1 : 0) + "," + f.getSymbol().getSource());
				n++;
			}
		}
		println("CDMP funciones = " + n + " por bloque " + perBlock);
	}

	private void strings(Path p) throws IOException {
		int n = 0, withRefs = 0;
		Map<String, Integer> perType = new TreeMap<>();
		ReferenceManager rm = currentProgram.getReferenceManager();
		try (PrintWriter w = open(p)) {
			w.println("rva,block,type,length,nrefs,value,refs");
			DataIterator it = currentProgram.getListing().getDefinedData(true);
			while (it.hasNext()) {
				Data d = it.next();
				if (!StringDataInstance.isString(d)) continue;
				String v = StringDataInstance.getStringDataInstance(d).getStringValue();
				if (v == null) continue;
				String type = d.getDataType().getName();
				perType.merge(type, 1, Integer::sum);
				StringBuilder refs = new StringBuilder();
				int nr = 0;
				for (Reference r : rm.getReferencesTo(d.getAddress())) {
					if (nr < 20) refs.append(nr > 0 ? ";" : "").append(at(r.getFromAddress()));
					nr++;
				}
				if (nr > 0) withRefs++;
				MemoryBlock b = currentProgram.getMemory().getBlock(d.getAddress());
				w.println(rva(d.getAddress()) + "," + (b == null ? "?" : b.getName()) + "," + csv(type) + "," + d.getLength() +
					"," + nr + "," + csv(v) + "," + csv(refs.toString()));
				n++;
			}
		}
		println("CDMP cadenas = " + n + " (con referencias " + withRefs + ") por tipo " + perType);
	}

	private boolean isCodePtr(Address t) {
		MemoryBlock b = currentProgram.getMemory().getBlock(t);
		if (b == null || !b.isInitialized()) return false;
		return b.isExecute() || b.getName().equals(".xtls") || b.getName().equals(".text");
	}

	private List<Address> slots(Address a) {
		List<Address> l = new ArrayList<>();
		Memory m = currentProgram.getMemory();
		for (int i = 0; i < 2048; i++) {
			Address slot = a.add(8L * i);
			if (i > 0) {
				Symbol s = getSymbolAt(slot);
				if (s != null && s.getSource() != SourceType.DEFAULT && !s.getName().startsWith("cdmp_")) break;
			}
			try {
				Address t = toAddr(m.getLong(slot));
				if (t == null || !isCodePtr(t)) break;
				l.add(t);
			}
			catch (Exception e) { break; }
		}
		return l;
	}

	private void rtti(Path p) throws IOException {
		SymbolTable st = currentProgram.getSymbolTable();
		Map<String, Address> td = new TreeMap<>();
		for (Symbol s : st.getSymbols("RTTI_Type_Descriptor")) td.put(s.getParentNamespace().getName(true), s.getAddress());
		Map<String, List<Address>> vts = new TreeMap<>();
		for (Symbol s : st.getSymbols("vftable")) vts.computeIfAbsent(s.getParentNamespace().getName(true), k -> new ArrayList<>()).add(s.getAddress());
		Set<String> classes = new TreeSet<>(td.keySet());
		classes.addAll(vts.keySet());
		int nvt = 0;
		try (PrintWriter w = open(p)) {
			w.println("class,type_descriptor_rva,vtable_rva,nslots,slots");
			for (String c : classes) {
				String tdr = td.containsKey(c) ? rva(td.get(c)) : "";
				List<Address> vl = vts.get(c);
				if (vl == null) { w.println(csv(c) + "," + tdr + ",,0,"); continue; }
				for (Address v : vl) {
					List<Address> sl = slots(v);
					StringBuilder sb = new StringBuilder();
					for (int i = 0; i < sl.size(); i++) sb.append(i > 0 ? ";" : "").append(rva(sl.get(i)));
					w.println(csv(c) + "," + tdr + "," + rva(v) + "," + sl.size() + "," + sb);
					nvt++;
				}
			}
		}
		println("CDMP RTTI: clases = " + classes.size() + " (con type descriptor " + td.size() + "), vftables = " + nvt);
	}

	private void imports(Path p) throws IOException {
		int n = 0;
		ReferenceManager rm = currentProgram.getReferenceManager();
		try (PrintWriter w = open(p)) {
			w.println("library,name,ptr_rva,uses,callers");
			for (Symbol s : currentProgram.getSymbolTable().getExternalSymbols()) {
				String lib = s.getParentNamespace().getName(true);
				Set<Address> callers = new LinkedHashSet<>();
				List<String> ptrs = new ArrayList<>();
				List<Address> via = new ArrayList<>();
				for (Reference r : s.getReferences()) via.add(r.getFromAddress());
				if (s.getObject() instanceof Function ef) {
					Address[] th = ef.getFunctionThunkAddresses(true);
					if (th != null) via.addAll(Arrays.asList(th));
				}
				for (Address v : via) {
					if (!v.isMemoryAddress()) continue;
					if (getInstructionAt(v) != null) { callers.add(v); continue; }
					ptrs.add(rva(v));
					for (Reference r : rm.getReferencesTo(v)) callers.add(r.getFromAddress());
				}
				StringBuilder sb = new StringBuilder();
				int i = 0;
				for (Address c : callers) { if (i++ >= 30) break; sb.append(i > 1 ? ";" : "").append(at(c)); }
				w.println(csv(lib) + "," + csv(s.getName()) + "," + csv(String.join(";", ptrs)) + "," + callers.size() + "," + csv(sb.toString()));
				n++;
			}
		}
		println("CDMP importaciones = " + n);
	}

	private void cdmpSymbols(Path p) throws IOException {
		int n = 0, fn = 0;
		try (PrintWriter w = open(p)) {
			w.println("rva,type,primary,name,block");
			for (Symbol s : currentProgram.getSymbolTable().getAllSymbols(false)) {
				if (!s.getName().startsWith("cdmp_")) continue;
				MemoryBlock b = currentProgram.getMemory().getBlock(s.getAddress());
				w.println(rva(s.getAddress()) + "," + s.getSymbolType() + "," + s.isPrimary() + "," + csv(s.getName(true)) + "," +
					(b == null ? "?" : b.getName()));
				n++;
				if (s.getSymbolType() == SymbolType.FUNCTION) fn++;
			}
		}
		println("CDMP símbolos cdmp_ = " + n + " (nombres de función = " + fn + ")");
	}
}
