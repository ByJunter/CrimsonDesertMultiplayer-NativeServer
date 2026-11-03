// cdmp: define las cadenas que el analizador "ASCII Strings" no ve: UTF-8 con coreano (hangul) y
// UTF-16LE, en los bloques de datos .xdata (el .rdata real) y .rdata (el .data real).
// Solo sobre bytes sin definir; heurísticas estrictas para no inventar cadenas:
//   UTF-8 : empieza tras un 0 (o al inicio), UTF-8 válido hasta el 0, todo imprimible, >= 2 caracteres
//           y al menos un carácter hangul (U+AC00–U+D7A3 o jamo U+3130–U+318F).
//   UTF-16: alineada a 2, empieza tras 0x0000, >= 5 caracteres hasta 0x0000, todos ASCII imprimible,
//           hangul o \t\r\n, y al menos 3 letras.
// Argumento opcional: dry (solo cuenta, no crea nada).
//@category cdmp
import java.nio.charset.*;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;

public class Cdmp_DefineStrings extends GhidraScript {
	private int u8 = 0, u16 = 0, u8ref = 0, u16ref = 0, skipped = 0;
	private boolean dry;

	@Override
	public void run() throws Exception {
		dry = Arrays.asList(getScriptArgs()).contains("dry");
		for (String bn : new String[] { ".xdata", ".rdata" }) {
			MemoryBlock b = currentProgram.getMemory().getBlock(bn);
			byte[] d = new byte[(int) b.getSize()];
			b.getBytes(b.getStart(), d);
			scanUtf8(b, d);
			scanUtf16(b, d);
			println(String.format("CDMP hasta %s (acumulado): utf8=%d (con refs %d) utf16=%d (con refs %d) saltadas(ya definido)=%d", bn, u8,
				u8ref, u16, u16ref, skipped));
		}
	}

	private static boolean hangul(int c) { return (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0x3130 && c <= 0x318F); }

	private static boolean printable(int c) {
		if (c == '\t' || c == '\n' || c == '\r') return true;
		if (c < 0x20 || c == 0x7F) return false;
		if (c >= 0x80 && c < 0xA0) return false;
		int t = Character.getType(c);
		return t != Character.CONTROL && t != Character.UNASSIGNED && t != Character.PRIVATE_USE && t != Character.SURROGATE;
	}

	private boolean free(Address a, int len) {
		Listing l = currentProgram.getListing();
		return l.isUndefined(a, a.add(len - 1));
	}

	private boolean referenced(Address a) { return currentProgram.getReferenceManager().hasReferencesTo(a); }

	private void scanUtf8(MemoryBlock b, byte[] d) throws Exception {
		CharsetDecoder dec = StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
			.onUnmappableCharacter(CodingErrorAction.REPORT);
		int i = 0;
		while (i < d.length) {
			if (i > 0 && d[i - 1] != 0) { i++; continue; }
			if (d[i] == 0) { i++; continue; }
			int e = i;
			boolean high = false;
			while (e < d.length && d[e] != 0 && e - i < 4096) { if ((d[e] & 0x80) != 0) high = true; e++; }
			if (e >= d.length || d[e] != 0 || !high) { i = e + 1; continue; }
			String s;
			try { s = dec.decode(java.nio.ByteBuffer.wrap(d, i, e - i)).toString(); }
			catch (CharacterCodingException x) { i = e + 1; continue; }
			boolean ok = s.codePointCount(0, s.length()) >= 2, han = false;
			for (int k = 0; k < s.length() && ok; ) {
				int c = s.codePointAt(k);
				if (!printable(c)) ok = false;
				if (hangul(c)) han = true;
				k += Character.charCount(c);
			}
			if (ok && han) {
				Address a = b.getStart().add(i);
				int len = e - i + 1;
				if (!free(a, len)) skipped++;
				else {
					if (!dry) {
						Data dt = createData(a, new TerminatedStringDataType());
						CharsetSettingsDefinition.CHARSET.setCharset(dt, "UTF-8");
					}
					u8++;
					if (referenced(a)) u8ref++;
				}
			}
			i = e + 1;
		}
	}

	private void scanUtf16(MemoryBlock b, byte[] d) throws Exception {
		for (int i = 0; i + 1 < d.length; i += 2) {
			if (i >= 2 && (d[i - 1] != 0 || d[i - 2] != 0)) continue;
			int e = i, letters = 0;
			boolean ok = true;
			StringBuilder sb = new StringBuilder();
			while (e + 1 < d.length) {
				int c = (d[e] & 0xFF) | ((d[e + 1] & 0xFF) << 8);
				if (c == 0) break;
				boolean good = (c >= 0x20 && c < 0x7F) || c == '\t' || c == '\n' || c == '\r' || hangul(c);
				if (!good) { ok = false; break; }
				if (Character.isLetter(c)) letters++;
				sb.append((char) c);
				e += 2;
				if (e - i > 8192) { ok = false; break; }
			}
			if (!ok || e + 1 >= d.length || sb.length() < 5 || letters < 3) { if (e > i) i = e; continue; }
			Address a = b.getStart().add(i);
			int len = e - i + 2;
			if (!free(a, len)) skipped++;
			else {
				if (!dry) createData(a, new TerminatedUnicodeDataType());
				u16++;
				if (referenced(a)) u16ref++;
			}
			i = e;
		}
	}
}
