#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include "core/error.h"

namespace cusage {

// User-tunable notification settings, read from a small key=value text file.
struct NotifyConfig {
  int poll_seconds = 600;
  bool notify_on_drop = true;     // Alert when a window's usage% decreases (freed).
  int weekly_threshold = 80;      // Weekly rising-edge alert %; 0 disables.
  int five_hour_drop_floor = 95;  // 5-hour drop alerts only when prev% >= this.
  std::string relay_url;          // Self-hosted relay URL to publish usage to.
  std::string relay_token;        // Optional shared secret for the relay.
};

// Parses key=value lines. Unknown keys are ignored; absent keys keep defaults.
// poll_seconds is floored to a sane minimum.
NotifyConfig parse_config(std::string_view text);

std::string serialize_config(const NotifyConfig& config);

// ~/.claude/cusage.conf for the current user.
std::expected<std::filesystem::path, Error> default_config_path();

// Reads config from `path`. If the file is absent, writes a commented default
// (so the user has something to edit) and returns defaults.
NotifyConfig load_config(const std::filesystem::path& path);

}  // namespace cusage
