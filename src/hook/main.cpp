// cusage_hook_notify — Claude Code hook forwarder.
//
// Registered as a Stop / UserPromptSubmit / SessionStart / SessionEnd hook (see
// PLAN.md §4). Claude Code delivers the hook payload as JSON on stdin; this
// program extracts a little metadata and POSTs it to the self-hosted relay so
// the desktop/phone widget can tell when a session has "landed" (gone idle).
//
// Design contract (PLAN.md §2.4, §2.6):
//   * Fire-and-forget: a short timeout and it ALWAYS exits 0 — an unreachable or
//     misconfigured relay must never stall or fail a Claude Code session.
//   * Metadata only: session_id, cwd, session_title, event, prompt_id, ts. The
//     transcript and last_assistant_message are never forwarded, nor is any
//     token ever logged.
//   * No relay configured -> do nothing (the feature is off on this machine).

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/config.h"
#include "core/json_parse.h"
#include "net/http_client.h"
#include "net/relay.h"
#include "platform/user_paths.h"

namespace {

// Optional troubleshooting log, only when CUSAGE_HOOK_DEBUG is set. Writes one
// line to ~/.claude/cusage_hook_notify.log. Never logs the relay token.
void debug_log(std::string_view line) {
  if (std::getenv("CUSAGE_HOOK_DEBUG") == nullptr) return;
  auto dir = cusage::platform::claude_config_dir();
  if (!dir) return;
  std::ofstream out(*dir / "cusage_hook_notify.log", std::ios::app);
  if (out) out << line << '\n';
}

std::string get_string(const nlohmann::json& doc, const char* key) {
  if (doc.contains(key) && doc[key].is_string()) {
    return doc[key].get<std::string>();
  }
  return {};
}

void forward() {
  const std::string input(
      (std::istreambuf_iterator<char>(std::cin)),
      std::istreambuf_iterator<char>());

  auto doc = cusage::parse_json(input);
  if (!doc) {
    debug_log(std::string("parse failed: ") + doc.error().message);
    return;
  }

  const std::string event = get_string(*doc, "hook_event_name");
  const std::string session_id = get_string(*doc, "session_id");
  const std::string cwd = get_string(*doc, "cwd");
  const std::string prompt_id = get_string(*doc, "prompt_id");
  const std::string session_title = get_string(*doc, "session_title");
  const std::size_t background_tasks =
      (doc->contains("background_tasks") && (*doc)["background_tasks"].is_array())
          ? (*doc)["background_tasks"].size()
          : 0;

  cusage::NotifyConfig config;
  if (auto path = cusage::default_config_path()) {
    config = cusage::load_config(*path);
  }
  if (config.relay_url.empty()) {
    debug_log("no relay_url configured; skipping (feature off)");
    return;  // Feature is off on this machine.
  }

  const long long ts = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

  nlohmann::json body = nlohmann::json::object();
  body["event"] = event;
  body["session_id"] = session_id;
  body["cwd"] = cwd;
  if (!prompt_id.empty()) body["prompt_id"] = prompt_id;
  if (!session_title.empty()) body["session_title"] = session_title;
  body["background_tasks"] = background_tasks;
  body["ts"] = ts;

  const std::string url = cusage::relay_base(config.relay_url) + "/session/event";
  std::vector<std::string> headers = {"Content-Type: application/json"};
  if (!config.relay_token.empty()) {
    headers.push_back("Authorization: Bearer " + config.relay_token);
  }

  // Short timeout: this runs inline before the session continues.
  auto response = cusage::http_post_json(url, body.dump(), headers, 2500L);
  if (!response) {
    debug_log(event + " -> " + url + " FAILED: " + response.error().message);
    return;
  }
  debug_log(event + " -> " + url + " HTTP " +
            std::to_string(response->status_));
}

}  // namespace

int main() {
  // A hook must never fail the session it runs in, so swallow everything and
  // always report success — the whole point is to be invisible when it can't
  // reach the relay.
  try {
    forward();
  } catch (const std::exception& e) {
    debug_log(std::string("unhandled exception: ") + e.what());
  } catch (...) {
    debug_log("unhandled non-standard exception");
  }
  return 0;
}
