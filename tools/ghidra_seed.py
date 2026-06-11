# ghidra_seed.py — Ghidra headless preScript.
#
# Raw-binary imports have no entry point, so auto-analysis has nothing to
# disassemble from. Seed the known code entries (entry stub + Main) here,
# BEFORE auto-analysis runs, so the MIPS analyzer can propagate through
# jal targets and discover the rest of the resident functions.
# Jython 2.7 compatible.
#
# Invoked by analyzeHeadless:
#   ... -preScript ghidra_seed.py 0x80000400 0x80000550
#
# @category N64Recomp
args = getScriptArgs()
seeds = list(args) if args else ["0x80000400", "0x80000550"]

af = currentProgram.getAddressFactory()
for s in seeds:
    addr = af.getAddress(s)
    if addr is None:
        print("[ghidra_seed] bad address: %s" % s)
        continue
    try:
        disassemble(addr)
        createFunction(addr, None)
        print("[ghidra_seed] seeded %s" % s)
    except Exception as e:
        print("[ghidra_seed] failed at %s: %s" % (s, e))
