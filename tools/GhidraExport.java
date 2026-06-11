// GhidraExport.java — Ghidra headless postScript (Java; no PyGhidra needed).
//
// Dumps every function (name, entry vram, byte size) and memory block from
// the analyzed program to a JSON file, for tools/gen_symbols_toml.py.
//
//   ... -postScript GhidraExport.java <out.json>
//
// @category N64Recomp
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.MemoryBlock;
import java.io.FileWriter;
import java.io.PrintWriter;

public class GhidraExport extends GhidraScript {
    private static String esc(String s) {
        return s.replace("\\", "\\\\").replace("\"", "\\\"");
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String out = (args.length > 0) ? args[0] : "functions.json";

        StringBuilder sb = new StringBuilder();
        sb.append("{\n");
        sb.append(" \"program\": \"").append(esc(currentProgram.getName())).append("\",\n");
        sb.append(" \"image_base\": ")
          .append(currentProgram.getImageBase().getOffset() & 0xFFFFFFFFL).append(",\n");

        FunctionManager fm = currentProgram.getFunctionManager();
        sb.append(" \"functions\": [\n");
        boolean first = true;
        int count = 0;
        for (Function f : fm.getFunctions(true)) {
            long entry = f.getEntryPoint().getOffset() & 0xFFFFFFFFL;
            long size = f.getBody().getNumAddresses();
            if (!first) sb.append(",\n");
            first = false;
            sb.append("  {\"name\": \"").append(esc(f.getName()))
              .append("\", \"vram\": ").append(entry)
              .append(", \"size\": ").append(size).append("}");
            count++;
        }
        sb.append("\n ],\n");
        sb.append(" \"function_count\": ").append(count).append(",\n");

        sb.append(" \"segments\": [\n");
        first = true;
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            if (!first) sb.append(",\n");
            first = false;
            sb.append("  {\"name\": \"").append(esc(b.getName()))
              .append("\", \"start\": ").append(b.getStart().getOffset() & 0xFFFFFFFFL)
              .append(", \"size\": ").append(b.getSize())
              .append(", \"exec\": ").append(b.isExecute()).append("}");
        }
        sb.append("\n ]\n}\n");

        PrintWriter w = new PrintWriter(new FileWriter(out));
        try {
            w.print(sb.toString());
        } finally {
            w.close();
        }
        println("[GhidraExport] wrote " + count + " functions -> " + out);
    }
}
