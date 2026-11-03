#!/bin/bash
# usage: dc.sh rva...  (uses cache in exports/decomp; retries on project lock)
D=/d/RE/CrimsonDesert_2976/exports/decomp
for a in "$@"; do
  r=$(printf "0x%x" $((a)))
  if [ -s "$D/$r.c" ]; then echo "cached $r"; continue; fi
  for i in 1 2 3 4 5 6 7 8 9 10; do
    out=$(powershell -NoProfile -ExecutionPolicy Bypass -File 'D:\RE\CrimsonDesert_2976\scripts\cdmp-query.ps1' decomp $(printf "0x%X" $((0x140000000 + a))) 2>&1)
    if echo "$out" | grep -q "Unable to lock"; then sleep 7; continue; fi
    echo "$r: $(echo "$out" | tail -1)"; break
  done
done
