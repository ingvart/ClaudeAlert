#include <cstdio>
#include <cstdlib>
#include <exception>
#include <expected>
#include <fstream>
#include <iterator>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/config.h"
#include "core/credentials.h"
#include "core/json_parse.h"
#include "core/oauth.h"
#include "core/poller.h"
#include "core/usage.h"
#include "core/usage_client.h"
#include "net/relay.h"

namespace {

void print_window(std::string_view label, const cusage::UsageWindow& window) {
  spdlog::info("{:<14} {:>3}%   resets {}", label, window.percent(),
               window.resets_at_ ? *window.resets_at_ : "unknown");
}

void print_snapshot(const cusage::UsageSnapshot& snapshot) {
  bool any = false;
  if (snapshot.five_hour_) {
    print_window("5-hour", *snapshot.five_hour_);
    any = true;
  }
  if (snapshot.seven_day_) {
    print_window("weekly", *snapshot.seven_day_);
    any = true;
  }
  for (const auto& [model, window] : snapshot.model_weekly_) {
    print_window("weekly:" + model, window);
    any = true;
  }
  if (snapshot.extra_usage_ && snapshot.extra_usage_->is_enabled_) {
    spdlog::info("extra usage enabled");
  }
  if (!any) spdlog::warn("no usage windows reported");
}

// Live mode: read the token from the credentials file (never written back) and
// fetch the current usage snapshot.
int run_live() {
  auto path = cusage::default_credentials_path();
  if (!path) {
    spdlog::error("{}", path.error().message);
    return EXIT_FAILURE;
  }
  auto tokens = cusage::read_oauth_tokens(*path);
  if (!tokens) {
    spdlog::error("{}", tokens.error().message);
    return EXIT_FAILURE;
  }
  spdlog::info("subscription: {}",
               tokens->subscription_type_.value_or("unknown"));
  if (cusage::token_expiring_soon(*tokens)) {
    spdlog::warn("access token at/near expiry — open Claude Code to refresh it");
  }

  auto usage = cusage::fetch_usage(tokens->access_token_);
  if (!usage) {
    spdlog::error("{}", usage.error().message);
    return EXIT_FAILURE;
  }
  print_snapshot(*usage);
  return EXIT_SUCCESS;
}

// Offline mode: parse a captured usage document, no network or token.
int run_offline(std::string_view file_path) {
  std::ifstream stream(std::string(file_path), std::ios::binary);
  if (!stream) {
    spdlog::error("cannot open {}", file_path);
    return EXIT_FAILURE;
  }
  const std::string text((std::istreambuf_iterator<char>(stream)),
                         std::istreambuf_iterator<char>());

  auto doc = cusage::parse_json(text);
  if (!doc) {
    spdlog::error("{}", doc.error().message);
    return EXIT_FAILURE;
  }
  auto usage = cusage::parse_usage(*doc);
  if (!usage) {
    spdlog::error("{}", usage.error().message);
    return EXIT_FAILURE;
  }
  print_snapshot(*usage);
  return EXIT_SUCCESS;
}

// Reports which credentials are available, without any network request and
// without ever printing the token value.
int report_credentials() {
  auto path = cusage::default_credentials_path();
  if (!path) {
    spdlog::error("{}", path.error().message);
    return EXIT_FAILURE;
  }
  spdlog::info("credentials path: {}", path->string());

  auto tokens = cusage::read_oauth_tokens(*path);
  if (!tokens) {
    spdlog::error("{}", tokens.error().message);
    return EXIT_FAILURE;
  }
  spdlog::info("access token: present ({} chars)", tokens->access_token_.size());
  spdlog::info("refresh token: {}",
               tokens->refresh_token_ ? "present" : "absent");
  spdlog::info("expires at: {}", tokens->expires_at_.value_or("unknown"));
  spdlog::info("subscription: {}",
               tokens->subscription_type_.value_or("unknown"));
  return EXIT_SUCCESS;
}

int run_help() {
  std::fputs(
      "Claude Usage Monitor (cusage)\n\n"
      "Usage:\n"
      "  cusage                 Fetch and print current usage (uses the local Claude token).\n"
      "  cusage --watch         Poll on an interval and print alerts (~/.claude/cusage.conf).\n"
      "  cusage --token         Print the current OAuth access token (to paste into the phone app).\n"
      "  cusage --creds         Show token presence/expiry (no network, no secrets).\n"
      "  cusage --file <path>   Parse a captured usage JSON document offline.\n"
      "  cusage --help          Show this help.\n\n"
      "Token for the Android app:\n"
      "  The desktop reads your token automatically. For a DURABLE phone token, run\n"
      "      claude setup-token\n"
      "  in a terminal; it opens a browser, you approve, and it prints a long-lived\n"
      "  token to paste into the phone app's \"OAuth token\" field.\n"
      "  `cusage --token` (or the tray app's \"Copy token\" item) gives the desktop's\n"
      "  current token, but that one expires after a few hours.\n",
      stdout);
  return EXIT_SUCCESS;
}

// Prints the current access token to stdout (for piping/copying). The expiry
// caveat goes to stderr so stdout stays just the token.
int run_token() {
  auto path = cusage::default_credentials_path();
  if (!path) {
    spdlog::error("{}", path.error().message);
    return EXIT_FAILURE;
  }
  auto tokens = cusage::read_oauth_tokens(*path);
  if (!tokens) {
    spdlog::error("{}", tokens.error().message);
    return EXIT_FAILURE;
  }
  std::fputs("# Expires in a few hours; for the phone prefer `claude setup-token`.\n",
             stderr);
  std::printf("%s\n", tokens->access_token_.c_str());
  return EXIT_SUCCESS;
}

// Watch mode: poll on an interval and report alerts to the console. The token
// is re-read from the credentials file each poll (never written), so Claude
// Code's own refreshes are picked up automatically.
int run_watch() {
  cusage::NotifyConfig config;
  if (auto config_path = cusage::default_config_path()) {
    config = cusage::load_config(*config_path);
    spdlog::info("config: {}", config_path->string());
  } else {
    spdlog::warn("no config path ({}); using defaults",
                 config_path.error().message);
  }

  cusage::UsageFetcher fetcher =
      []() -> std::expected<cusage::UsageSnapshot, cusage::Error> {
    auto path = cusage::default_credentials_path();
    if (!path) return std::unexpected(path.error());
    auto tokens = cusage::read_oauth_tokens(*path);
    if (!tokens) return std::unexpected(tokens.error());
    return cusage::fetch_usage(tokens->access_token_);
  };

  const std::string relay_url = config.relay_url;
  const std::string relay_token = config.relay_token;
  cusage::UsageMonitor monitor(config, std::move(fetcher));
  std::stop_source stop;  // Process is stopped via Ctrl+C.
  spdlog::info("watching usage every {}s (Ctrl+C to stop)", config.poll_seconds);
  cusage::run_poll_loop(
      monitor, stop.get_token(),
      [relay_url, relay_token](const cusage::PollResult& result) {
        if (result.error) {
          spdlog::warn("poll error: {}", result.error->message);
          return;
        }
        if (result.snapshot) {
          print_snapshot(*result.snapshot);
          if (!relay_url.empty()) {
            if (auto pub = cusage::publish_usage(relay_url, relay_token,
                                                 *result.snapshot);
                !pub) {
              spdlog::warn("relay publish failed: {}", pub.error().message);
            }
          }
        }
        for (const auto& alert : result.alerts) {
          spdlog::warn("ALERT: {}", alert.message);
        }
      });
  return EXIT_SUCCESS;
}

int run(int argc, char** argv) {
  const std::vector<std::string_view> args(argv + 1, argv + argc);
  if (args.empty()) return run_live();
  const std::string_view command = args.front();
  if (command == "--help" || command == "-h") return run_help();
  if (command == "--watch") return run_watch();
  if (command == "--token") return run_token();
  if (command == "--creds") return report_credentials();
  if (command == "--file" && args.size() >= 2) return run_offline(args[1]);
  if (command.starts_with("--")) {
    spdlog::error("unknown option: {}", command);
    return run_help();
  }
  return run_offline(command);
}

}  // namespace

int main(int argc, char** argv) {
  // Top-level backstop: turn an unexpected std throw into a clean exit instead
  // of std::terminate. Not control flow — expected failures return an Error.
  try {
    return run(argc, argv);
  } catch (const std::exception& e) {
    spdlog::critical("unhandled exception: {}", e.what());
    return EXIT_FAILURE;
  } catch (...) {
    spdlog::critical("unhandled non-standard exception");
    return EXIT_FAILURE;
  }
}
