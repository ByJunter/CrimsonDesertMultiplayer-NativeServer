import pathlib,json
p=pathlib.Path(__file__).parent
s=p.joinpath('estatico-disasm.txt').read_text()
for line in s.splitlines():
 if line.startswith('FUNCTION') or '[rax + 0x2f0]' in line or '0x14043db70' in line or '+ 0x2f8]' in line:print(line)
