# ghidra_export.py — Ghidra headless postScript.
#
# Dumps every function (name, entry vram, byte size) and memory block
# from the analyzed program to a JSON file, for tools/gen_symbols_toml.py.
# Jython 2.7 compatible (the analyzeHeadless default interpreter).
#
# Invoked by analyzeHeadless:
#   ... -postScript ghidra_export.py <out.json>
#
# @category N64Recomp
import json

args = getScriptArgs()
out_path = args[0] if args else "functions.json"

fm = currentProgram.getFunctionManager()
functions = []
for func in fm.getFunctions(True):          # True = forward order
    entry = func.getEntryPoint()
    functions.append({
        "name": func.getName(),
        "vram": entry.getOffset() & 0xFFFFFFFF,
        "size": int(func.getBody().getNumAddresses()),
    })

blocks = []
for b in currentProgram.getMemory().getBlocks():
    blocks.append({
        "name": b.getName(),
        "start": b.getStart().getOffset() & 0xFFFFFFFF,
        "size": int(b.getSize()),
        "exec": bool(b.isExecute()),
    })

doc = {
    "program": currentProgram.getName(),
    "image_base": currentProgram.getImageBase().getOffset() & 0xFFFFFFFF,
    "function_count": len(functions),
    "functions": functions,
    "segments": blocks,
}

f = open(out_path, "w")
try:
    json.dump(doc, f, indent=1)
finally:
    f.close()

print("[ghidra_export] wrote %d functions -> %s" % (len(functions), out_path))
