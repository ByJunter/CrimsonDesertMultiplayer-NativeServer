// cdmp: deshace Cdmp_PreAnalysis.java: devuelve a .xtls su permiso de ejecución original (RWX en el
// PE) y quita la restricción "Restrict Disassembly to Executable Memory". No borra nada del análisis.
// Uso: analyzeHeadless <proyecto> CrimsonDesert_2976 -process CrimsonDesert.exe -noanalysis -postScript Cdmp_RevertXtls.java
//@category cdmp
import ghidra.app.script.GhidraScript;
import ghidra.framework.options.Options;
import ghidra.program.disassemble.Disassembler;
import ghidra.program.model.listing.Program;
import ghidra.program.model.mem.MemoryBlock;

public class Cdmp_RevertXtls extends GhidraScript {
	@Override
	public void run() throws Exception {
		MemoryBlock xtls = currentProgram.getMemory().getBlock(".xtls");
		// El permiso original se lee de la tabla de secciones del PE (0xE0000020 = RWX en la build 2976).
		String pe = pePerm(currentProgram, ".xtls");
		xtls.setExecute(pe == null || pe.endsWith("X"));
		currentProgram.getOptions(Program.DISASSEMBLER_PROPERTIES)
			.setBoolean(Disassembler.RESTRICT_DISASSEMBLY_TO_EXECUTE_MEMORY_PROPERTY, false);
		println("CDMP .xtls execute = " + xtls.isExecute() + "; restricción de desensamblado quitada");
	}

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
}
