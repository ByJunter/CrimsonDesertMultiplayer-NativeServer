// cdmp: corrige "cdmp.xtls.execute.original" en Program Information con el permiso real del PE.
// (La primera versión de Cdmp_PreAnalysis.java lo sobrescribió con false al ejecutarse por segunda vez.)
//@category cdmp
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Program;

public class Cdmp_SetXtlsOrig extends GhidraScript {
	@Override
	public void run() throws Exception {
		String pe = Cdmp_RevertXtls.pePerm(currentProgram, ".xtls");
		String v = String.valueOf(pe != null && pe.endsWith("X"));
		currentProgram.getOptions(Program.PROGRAM_INFO).setString("cdmp.xtls.execute.original", v);
		println("CDMP permisos PE de .xtls = " + pe + "; cdmp.xtls.execute.original = " + v);
	}
}
