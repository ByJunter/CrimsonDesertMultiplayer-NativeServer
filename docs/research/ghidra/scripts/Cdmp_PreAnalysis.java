// cdmp: preparación antes del análisis automático (build 1.0.0.2976, PE ts 0x6AB28F00).
//  1. Quita el permiso de ejecución al bloque .xtls (código virtualizado por Denuvo).
//  2. Activa la opción de programa "Disassembler > Restrict Disassembly to Executable Memory",
//     para que ningún flujo (call/jmp desde .rsrc) entre a .xtls ni a otros bloques no ejecutables.
//  3. Comprueba que .rsrc sigue siendo ejecutable (ahí está el código real a pesar del nombre).
//  4. Fija los analizadores (los que se desactivan, explícitamente).
// Reversible: Cdmp_RevertXtls.java vuelve a poner X en .xtls y quita la restricción.
//@category cdmp
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.framework.options.Options;
import ghidra.program.disassemble.Disassembler;
import ghidra.program.model.listing.Program;
import ghidra.program.model.mem.MemoryBlock;

public class Cdmp_PreAnalysis extends GhidraScript {
	@Override
	public void run() throws Exception {
		Program p = currentProgram;
		MemoryBlock xtls = p.getMemory().getBlock(".xtls");
		MemoryBlock rsrc = p.getMemory().getBlock(".rsrc");
		if (xtls == null || rsrc == null) throw new Exception("faltan bloques .xtls/.rsrc");
		if (!p.getImageBase().toString().equals("140000000"))
			throw new Exception("image base inesperada: " + p.getImageBase());

		Options info = p.getOptions(Program.PROGRAM_INFO);
		// Solo la primera vez: en ejecuciones posteriores .xtls ya no es ejecutable.
		if (!info.contains("cdmp.xtls.execute.original"))
			info.setString("cdmp.xtls.execute.original", String.valueOf(xtls.isExecute()));
		xtls.setExecute(false);
		println("CDMP .xtls execute -> " + xtls.isExecute() + " (original guardado en Program Information)");

		if (!rsrc.isExecute()) {
			rsrc.setExecute(true);
			println("CDMP .rsrc no era ejecutable: se le puso X");
		}
		println("CDMP .rsrc R=" + rsrc.isRead() + " W=" + rsrc.isWrite() + " X=" + rsrc.isExecute());

		Options dis = p.getOptions(Program.DISASSEMBLER_PROPERTIES);
		dis.setBoolean(Disassembler.RESTRICT_DISASSEMBLY_TO_EXECUTE_MEMORY_PROPERTY, true);
		println("CDMP " + Program.DISASSEMBLER_PROPERTIES + "." +
			Disassembler.RESTRICT_DISASSEMBLY_TO_EXECUTE_MEMORY_PROPERTY + " = " +
			dis.getBoolean(Disassembler.RESTRICT_DISASSEMBLY_TO_EXECUTE_MEMORY_PROPERTY, false));

		// Analizadores: los de por defecto + RTTI + cadenas; fuera solo los lentos y de poco valor.
		Map<String, String> want = new LinkedHashMap<>();
		want.put("Windows x86 PE RTTI Analyzer", "true");
		want.put("ASCII Strings", "true");
		want.put("Aggressive Instruction Finder", "false");
		want.put("Decompiler Parameter ID", "false");
		String extra = System.getenv("CDMP_DISABLE_ANALYZERS"); // lista separada por ';' (opcional)
		if (extra != null) for (String a : extra.split(";")) if (!a.isBlank()) want.put(a.trim(), "false");
		Map<String, String> cur = getCurrentAnalysisOptionsAndValues(p);
		for (Map.Entry<String, String> e : want.entrySet()) {
			if (!cur.containsKey(e.getKey())) { println("CDMP WARN analizador desconocido: " + e.getKey()); continue; }
			setAnalysisOption(p, e.getKey(), e.getValue());
		}
		cur = getCurrentAnalysisOptionsAndValues(p);
		for (Map.Entry<String, String> e : new TreeMap<>(cur).entrySet())
			if (!e.getKey().contains(".")) println("CDMP analyzer " + e.getKey() + " = " + e.getValue());
	}
}
