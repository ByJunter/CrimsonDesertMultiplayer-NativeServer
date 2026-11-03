#!/bin/bash
# usage: dc.sh <rva hex without 0x> ... ; saves decomp into dc/0x<rva>.c
S=<scratch>/s44/nativo
for r in "$@"; do
  f=$S/dc/0x$r.c
  if [ -f D:/RE/CrimsonDesert_2976/exports/decomp/0x$r.c ]; then cp D:/RE/CrimsonDesert_2976/exports/decomp/0x$r.c $f; continue; fi
  [ -s $f ] && continue
  va=$(printf "0x%X" $((0x140000000 + 0x$r)))
  powershell -NoProfile -ExecutionPolicy Bypass -File 'D:\RE\CrimsonDesert_2976\scripts\cdmp-query.ps1' decomp $va > $f 2>&1
done
