#pragma once

#include <expected>
#include <filesystem>

#include "core/error.h"

namespace cusage::platform {

// Absolute path to the user's ~/.claude directory, resolved per-OS so callers
// never see a platform #ifdef. Errors if the home/profile directory is unset.
std::expected<std::filesystem::path, Error> claude_config_dir();

}  // namespace cusage::platform
