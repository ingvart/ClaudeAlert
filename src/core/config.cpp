#include "core/config.h"

#include <charconv>
#include <fstream>
#include <iterator>
#include <string>

#include "platform/user_paths.h"

namespace cusage {

namespace {

std::string_view trim(std::string_view text) {
  const auto begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string_view::npos) return {};
  const auto end = text.find_last_not_of(" \t\r\n");
  return text.substr(begin, end - begin + 1);
}

bool parse_bool(std::string_view value, bool fallback) {
  if (value == "true" || value == "1" || value == "yes" || value == "on") {
    return true;
  }
  if (value == "false" || value == "0" || value == "no" || value == "off") {
    return false;
  }
  return fallback;
}

int parse_int(std::string_view value, int fallback) {
  int out = 0;
  const auto [ptr, ec] =
      std::from_chars(value.data(), value.data() + value.size(), out);
  return ec == std::errc() ? out : fallback;
}

}  // namespace

NotifyConfig parse_config(std::string_view text) {
  NotifyConfig config;
  std::size_t pos = 0;
  while (pos <= text.size()) {
    const auto newline = text.find('\n', pos);
    const std::size_t len =
        (newline == std::string_view::npos) ? text.size() - pos : newline - pos;
    const std::string_view line = trim(text.substr(pos, len));
    pos = (newline == std::string_view::npos) ? text.size() + 1 : newline + 1;

    if (line.empty() || line.front() == '#') continue;
    const auto eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    const std::string_view key = trim(line.substr(0, eq));
    const std::string_view value = trim(line.substr(eq + 1));

    if (key == "poll_seconds") {
      config.poll_seconds = parse_int(value, config.poll_seconds);
    } else if (key == "notify_on_drop") {
      config.notify_on_drop = parse_bool(value, config.notify_on_drop);
    } else if (key == "weekly_threshold") {
      config.weekly_threshold = parse_int(value, config.weekly_threshold);
    } else if (key == "five_hour_drop_floor") {
      config.five_hour_drop_floor =
          parse_int(value, config.five_hour_drop_floor);
    } else if (key == "relay_url") {
      config.relay_url = std::string(value);
    } else if (key == "relay_token") {
      config.relay_token = std::string(value);
    } else if (key == "session_poll_seconds") {
      config.session_poll_seconds =
          parse_int(value, config.session_poll_seconds);
    } else if (key == "session_notify_min_seconds") {
      config.session_notify_min_seconds =
          parse_int(value, config.session_notify_min_seconds);
    }
  }

  if (config.poll_seconds < 5) config.poll_seconds = 5;  // Avoid hammering.
  if (config.session_poll_seconds < 5) config.session_poll_seconds = 5;
  return config;
}

std::string serialize_config(const NotifyConfig& config) {
  std::string out;
  out += "# Claude Usage Monitor configuration\n";
  out += "# Edit and save; the monitor re-reads this when restarted.\n\n";
  out += "poll_seconds = " + std::to_string(config.poll_seconds) + "\n";
  out += std::string("notify_on_drop = ") +
         (config.notify_on_drop ? "true" : "false") + "\n";
  out += "# Weekly rising-edge alert percent; set to 0 to disable.\n";
  out += "weekly_threshold = " + std::to_string(config.weekly_threshold) + "\n";
  out += "# 5-hour drop alarm only fires when usage was at/above this before "
         "dropping.\n";
  out += "five_hour_drop_floor = " +
         std::to_string(config.five_hour_drop_floor) + "\n";
  out += "# Self-hosted relay so the phone can read usage (see relay/relay.py).\n";
  out += "relay_url = " + config.relay_url + "\n";
  out += "relay_token = " + config.relay_token + "\n";
  out += "# Session 'landed' notifications (needs a relay_url; see PLAN.md).\n";
  out += "session_poll_seconds = " +
         std::to_string(config.session_poll_seconds) + "\n";
  out += "session_notify_min_seconds = " +
         std::to_string(config.session_notify_min_seconds) + "\n";
  return out;
}

std::expected<std::filesystem::path, Error> default_config_path() {
  auto dir = platform::claude_config_dir();
  if (!dir) return std::unexpected(dir.error());
  return *dir / "cusage.conf";
}

NotifyConfig load_config(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    const NotifyConfig defaults;
    std::ofstream out(path, std::ios::binary);
    if (out) out << serialize_config(defaults);
    return defaults;
  }
  const std::string text((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
  return parse_config(text);
}

}  // namespace cusage
