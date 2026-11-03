// cdmp: repara la primera pasada de Cdmp_ApplyNames.java, que renombró funciones que ya tenían un nombre
// con espacio de nombres (demanglado, p. ej. AK::TempAlloc::X) y dejó mezclas como
// AK::TempAlloc::cdmp_Y. Les devuelve su nombre anterior (columna "antes" del informe) y deja el
// nombre cdmp_ como etiqueta secundaria. Argumento: report=<cdmp_names_report.csv>
//@category cdmp
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.*;

public class Cdmp_FixNamespacedNames extends GhidraScript {
	@Override
	public void run() throws Exception {
		String report = "D:\\RE\\CrimsonDesert_2976\\exports\\cdmp_names_report.csv";
		for (String s : getScriptArgs()) if (s.startsWith("report=")) report = s.substring(7);
		Map<Long, String> before = new HashMap<>();
		for (List<String> r : Cdmp_ApplyNames.readCsv(report)) {
			if (r.size() < 5 || !r.get(3).equals("funcion_renombrada")) continue;
			int i = r.get(4).indexOf("antes ");
			if (i < 0) continue;
			before.put(Long.parseLong(r.get(0).substring(2), 16), r.get(4).substring(i + 6).trim());
		}
		int fixed = 0;
		for (Map.Entry<Long, String> e : before.entrySet()) {
			Address a = toAddr(0x140000000L + e.getKey());
			Function f = getFunctionAt(a);
			if (f == null || f.getParentNamespace().isGlobal() || !f.getName().startsWith("cdmp_")) continue;
			String cdmp = f.getName(), ns = f.getParentNamespace().getName(true);
			f.setName(e.getValue(), SourceType.ANALYSIS);
			createLabel(a, cdmp, false, SourceType.USER_DEFINED);
			println("CDMP " + String.format("0x%X", e.getKey()) + " " + ns + "::" + cdmp + " -> " + f.getName(true) + " (+etiqueta " + cdmp + ")");
			fixed++;
		}
		println("CDMP funciones reparadas = " + fixed);
	}
}
