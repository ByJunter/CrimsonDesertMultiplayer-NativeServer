// cdmp: ejecuta el analizador RTTI de Windows con los nombres de sección "reales" y luego los restaura.
//
// Motivo: Denuvo renombró las secciones y el RttiAnalyzer de Ghidra busca bloques por nombre:
//   - type descriptors (.?AV…) en bloques ".data*"  -> aquí están en el bloque ".rdata" (RW, el .data real);
//   - Complete Object Locators en ".rdata"/".data"/".text" -> aquí en ".xdata" (R, el .rdata real);
//   - entradas de vtable: si existe un bloque ".text" o ".nep" deben caer en él; aquí ".text" es un
//     bloque de Denuvo sin código, así que con él no valdría ninguna vtable. Sin ".text" acepta
//     cualquier dirección cargada que sea función (código en .rsrc y métodos virtualizados en .xtls).
// Pasos: .text -> .text_dnv, .rdata -> .data, .xdata -> .rdata; RttiAnalyzer.added(); y deshace los
// nombres en orden inverso (también si falla). Solo crea datos y símbolos RTTI; no toca permisos.
//@category cdmp
import ghidra.app.plugin.prototype.MicrosoftCodeAnalyzerPlugin.RttiAnalyzer;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.importer.MessageLog;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class Cdmp_RttiFix extends GhidraScript {
	private final String[][] ren = { { ".text", ".text_dnv" }, { ".rdata", ".data" }, { ".xdata", ".rdata" } };

	@Override
	public void run() throws Exception {
		Memory m = currentProgram.getMemory();
		int before = count("vftable"), tdBefore = count("RTTI_Type_Descriptor");
		println("CDMP antes: vftable=" + before + " RTTI_Type_Descriptor=" + tdBefore);
		int done = 0;
		try {
			for (String[] r : ren) {
				MemoryBlock b = m.getBlock(r[0]);
				if (b == null) throw new Exception("falta el bloque " + r[0]);
				b.setName(r[1]);
				done++;
				println("CDMP renombrado " + r[0] + " -> " + r[1]);
			}
			// El analizador no se repite si ya dejó "RTTI Found" en Program Information (lo puso a
			// false en el análisis automático, cuando no encontró nada por los nombres de sección).
			ghidra.framework.options.Options pi = currentProgram.getOptions("Program Information");
			if (pi.contains("RTTI Found")) {
				println("CDMP 'RTTI Found' estaba en " + pi.getObject("RTTI Found", null) + "; se quita para repetir");
				pi.removeOption("RTTI Found");
			}
			MessageLog log = new MessageLog();
			long t0 = System.nanoTime();
			boolean ok = new RttiAnalyzer().added(currentProgram, m, monitor, log);
			println(String.format("CDMP RttiAnalyzer.added = %s en %.1f s", ok, (System.nanoTime() - t0) / 1e9));
			String msgs = log.toString();
			if (!msgs.isBlank()) println("CDMP mensajes RTTI (primeros 3000 car.): " + msgs.substring(0, Math.min(3000, msgs.length())));
		}
		finally {
			for (int i = done - 1; i >= 0; i--) {
				m.getBlock(ren[i][1]).setName(ren[i][0]);
				println("CDMP restaurado " + ren[i][1] + " -> " + ren[i][0]);
			}
		}
		println("CDMP después: vftable=" + count("vftable") + " RTTI_Type_Descriptor=" + count("RTTI_Type_Descriptor"));
	}

	private int count(String name) {
		int n = 0;
		for (Symbol s : currentProgram.getSymbolTable().getSymbols(name)) n++;
		return n;
	}
}
