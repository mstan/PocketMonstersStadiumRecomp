#pragma once

// Runtime file locations for the PocketMonstersStadiumRecomp executable.
//
// Portable layout: every file the program writes at runtime (rom.cfg, crash
// logs) lives next to the executable rather than at any build-tree path, so a
// released binary never depends on a developer's directory layout.

#include <filesystem>
#include <string>

namespace pms {

// Directory containing the running executable.
std::filesystem::path exe_dir();

// Absolute path for a runtime-written file (config, log, dump), next to the exe.
std::filesystem::path app_file(const std::string& name);

} // namespace pms
