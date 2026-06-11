// GhidraSeed.java — Ghidra headless preScript (Java; no PyGhidra needed).
//
// Raw-binary imports have no entry point, so auto-analysis has nothing to
// disassemble from. Seed the known code entries (entry stub + Main) here,
// BEFORE auto-analysis runs, so the MIPS analyzer can propagate through
// jal targets and discover the rest of the resident functions.
//
//   ... -preScript GhidraSeed.java 0x80000400 0x80000550
//
// @category N64Recomp
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class GhidraSeed extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String[] seeds = (args.length > 0) ? args
                : new String[] { "0x80000400", "0x80000550" };
        for (String s : seeds) {
            Address addr = currentProgram.getAddressFactory().getAddress(s);
            if (addr == null) {
                println("[GhidraSeed] bad address: " + s);
                continue;
            }
            try {
                disassemble(addr);
                createFunction(addr, null);
                println("[GhidraSeed] seeded " + s);
            } catch (Exception e) {
                println("[GhidraSeed] failed at " + s + ": " + e);
            }
        }
    }
}
