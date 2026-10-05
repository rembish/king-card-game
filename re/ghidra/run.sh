#!/bin/sh
# Import KING.EXE (short variant) or KING2.EXE (full variant) into a headless Ghidra project
# and dump decompiled C to king_decomp.c / king2_decomp.c.
# Usage: run.sh [king|king2]   (default: both). Needs GHIDRA (default ~/tools/ghidra_*).
set -e
cd "$(dirname "$0")"
GHIDRA=${GHIDRA:-$(ls -d ~/tools/ghidra_*_PUBLIC | tail -1)}
mkdir -p proj
for v in ${1:-king king2}; do
    EXE=$(echo "$v" | tr a-z A-Z).EXE
    touch "names_$v.txt"
    "$GHIDRA/support/analyzeHeadless" "$PWD/proj" "$v" -import "$PWD/../../original/$EXE" -overwrite \
        -scriptPath "$PWD" -postScript ApplyNames.java "$PWD/names_$v.txt" \
        -postScript DumpAll.java "$PWD/${v}_decomp.c" > "headless_$v.log" 2>&1
    echo "$v: $(grep -c '=====' "${v}_decomp.c") functions"
done
