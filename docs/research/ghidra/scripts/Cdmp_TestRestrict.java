// cdmp: prueba (usar con -readOnly) de que el desensamblado entra en .rsrc y no en .xtls.
//@category cdmp
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;

public class Cdmp_TestRestrict extends GhidraScript {
	@Override
	public void run() throws Exception {
		MemoryBlock xtls = currentProgram.getMemory().getBlock(".xtls");
		Function inX = null;
		for (Function f : currentProgram.getFunctionManager().getFunctions(
			new AddressSet(xtls.getStart(), xtls.getEnd()), true)) { inX = f; break; }
		// 0x1318E70: accesor de .rsrc que salta a .xtls; 0x1317D40: reportero de Sentry.
		long[] rvas = { 0x1317D40L, 0x1318E70L, inX.getEntryPoint().getOffset() - 0x140000000L };
		for (long rva : rvas) {
			Address a = toAddr(0x140000000L + rva);
			DisassembleCommand c = new DisassembleCommand(a, null, true);
			c.applyTo(currentProgram, monitor);
			AddressSetView s = c.getDisassembledAddressSet();
			long n = 0;
			for (Instruction i : currentProgram.getListing().getInstructions(s, true)) n++;
			Instruction first = getInstructionAt(a);
			println(String.format("CDMP disasm rva=0x%x block=%s instr=%d first=%s status=%s", rva,
				currentProgram.getMemory().getBlock(a).getName(), n, first, c.getStatusMsg()));
			// cuántas instrucciones quedaron en .xtls tras seguir los flujos
			long nx = 0;
			for (Instruction i : currentProgram.getListing().getInstructions(
				new AddressSet(xtls.getStart(), xtls.getEnd()), true)) nx++;
			println("CDMP   instrucciones en .xtls = " + nx);
		}
	}
}
