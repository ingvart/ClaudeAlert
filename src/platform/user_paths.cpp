#include "platform/user_paths.h"

#include <cstdlib>

namespace cusage::platform {

namespace {

std::expected<std::filesystem::path, Error> home_dir() {
#if defined(_WIN32)
  if (const char* profile = std::getenv("USERPROFILE"); profile && *profile) {
    return std::filesystem::path(profile);
  }
  return std::unexpected(Error{ErrorCode::io_error, "USERPROFILE is not set"});
#else
  if (const char* home = std::getenv("HOME"); home && *home) {
    return std::filesystem::path(home);
  }
  return std::unexpected(Error{ErrorCode::io_error, "HOME is not set"});
#endif
}

}  // namespace

std::expected<std::filesystem::path, Error> claude_config_dir() {
  auto home = home_dir();
  if (!home) return std::unexpected(home.error());
  return *home / ".claude";
}

}  // namespace cusage::platform
