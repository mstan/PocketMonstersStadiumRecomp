/*
 * register_overlays.cpp — feed the generated section table into librecomp's
 * recomp::overlays::register_overlays().
 *
 * The generated/recomp_overlays.inl defines:
 *   SectionTableEntry section_table[]            (every code section)
 *   int               overlay_sections_by_index[] (overlay slot -> section)
 *   size_t            num_sections                (total sections)
 *
 * Without this registration librecomp's func_map stays empty for every section
 * beyond patches, so any LOOKUP_FUNC at runtime fails with "Failed to find
 * function at 0x...". For PMS this currently registers just the resident kernel
 * (one section at vram 0x80000400); overlays are not yet sectioned.
 */

#include <cstdint>
#include <cstddef>

#include "librecomp/overlays.hpp"

#define ARRLEN(x) (sizeof(x) / sizeof((x)[0]))

#include "../../generated/recomp_overlays.inl"

namespace pms {
    void register_overlays();
}

void pms::register_overlays() {
    recomp::overlays::overlay_section_table_data_t sections {
        .code_sections      = section_table,
        .num_code_sections  = ARRLEN(section_table),
        .total_num_sections = num_sections,
    };

    recomp::overlays::overlays_by_index_t overlays {
        .table = overlay_sections_by_index,
        .len   = ARRLEN(overlay_sections_by_index),
    };

    recomp::overlays::register_overlays(sections, overlays);
}
