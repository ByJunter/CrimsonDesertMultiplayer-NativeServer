// cdmp: inspección del programa importado (solo lectura).
// Imprime base de imagen, bloques de memoria con permisos, funciones/instrucciones por bloque,
// puntos de entrada, opciones de desensamblado y la lista de analizadores con su valor.
//@category cdmp
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.framework.options.Options;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class Cdmp_Inspect extends GhidraScript {
	@Override
	public void run() throws Exception {
		Program p = currentProgram;
		println("CDMP name=" + p.getName() + " lang=" + p.getLanguageID() + " compiler=" +
			p.getCompilerSpec().getCompilerSpecID());
		println("CDMP imageBase=" + p.getImageBase());
		println("CDMP exePath=" + p.getExecutablePath() + " sha256=" + p.getExecutableSHA256());
		Listing l = p.getListing();
		println("CDMP functions(total)=" + p.getFunctionManager().getFunctionCount() +
			" instructions(total)=" + l.getNumInstructions() + " definedData=" + l.getNumDefinedData());
		for (MemoryBlock b : p.getMemory().getBlocks()) {
			AddressSet s = new AddressSet(b.getStart(), b.getEnd());
			int nf = 0;
			FunctionIterator fi = p.getFunctionManager().getFunctions(s, true);
			while (fi.hasNext()) { fi.next(); nf++; }
			long ni = 0;
			InstructionIterator ii = l.getInstructions(s, true);
			while (ii.hasNext()) { ii.next(); ni++; }
			println(String.format("CDMP block %-10s %s-%s size=0x%x %s%s%s%s init=%s type=%s funcs=%d instr=%d",
				b.getName(), b.getStart(), b.getEnd(), b.getSize(), b.isRead() ? "R" : "-",
				b.isWrite() ? "W" : "-", b.isExecute() ? "X" : "-", b.isVolatile() ? "V" : "",
				b.isInitialized(), b.getType(), nf, ni));
		}
		AddressIterator ei = p.getSymbolTable().getExternalEntryPointIterator();
		int ne = 0;
		while (ei.hasNext()) {
			Address a = ei.next();
			if (ne++ < 40) {
				MemoryBlock b = p.getMemory().getBlock(a);
				println("CDMP entry " + a + " in " + (b == null ? "?" : b.getName()) + " " +
					p.getSymbolTable().getPrimarySymbol(a));
			}
		}
		println("CDMP entryPoints=" + ne);
		Options d = p.getOptions("Disassembler");
		for (String n : d.getOptionNames()) println("CDMP disasmOpt " + n + " = " + d.getObject(n, null));
		Map<String, String> ao = getCurrentAnalysisOptionsAndValues(p);
		for (Map.Entry<String, String> e : new TreeMap<>(ao).entrySet()) {
			if (!e.getKey().contains("."))
				println("CDMP analyzer " + e.getKey() + " = " + e.getValue());
		}
		Options pi = p.getOptions(Program.PROGRAM_INFO);
		println("CDMP analyzed=" + pi.getBoolean(Program.ANALYZED_OPTION_NAME, false));
	}
}
