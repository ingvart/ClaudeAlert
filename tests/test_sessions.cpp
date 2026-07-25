#include <set>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "core/session.h"
#include "core/session_client.h"

using namespace cusage;

TEST_CASE("parse_sessions reads an empty inventory", "[sessions]") {
  const auto doc = nlohmann::json::parse(
      R"({"sessions": [], "landings": [], "now": 1784586686.6})");
  const auto inv = parse_sessions(doc);
  REQUIRE(inv);
  CHECK(inv->sessions.empty());
  CHECK(inv->landings.empty());
  CHECK(inv->now == 1784586686);  // Float `now` truncated to whole seconds.
}

TEST_CASE("parse_sessions maps sessions and landings", "[sessions]") {
  const auto doc = nlohmann::json::parse(R"({
    "sessions": [
      {"session_id": "abc123", "cwd": "/home/x", "title": "long turn",
       "state": "idle", "last_seen": 1000, "last_landed_at": 990,
       "last_duration": 65},
      {"session_id": "def456", "cwd": "/home/y", "title": "", "state": "working",
       "last_seen": 1010, "last_landed_at": null, "last_duration": null}
    ],
    "landings": [
      {"session_id": "abc123", "title": "long turn", "cwd": "/home/x",
       "landed_at": 990, "duration": 65}
    ],
    "now": 1020
  })");
  const auto inv = parse_sessions(doc);
  REQUIRE(inv);
  REQUIRE(inv->sessions.size() == 2);
  CHECK(inv->sessions[0].session_id == "abc123");
  CHECK(inv->sessions[0].state == "idle");
  REQUIRE(inv->sessions[0].last_landed_at.has_value());
  CHECK(*inv->sessions[0].last_landed_at == 990);
  REQUIRE(inv->sessions[0].last_duration.has_value());
  CHECK(*inv->sessions[0].last_duration == 65);
  // A working session that has never landed reports null -> nullopt.
  CHECK_FALSE(inv->sessions[1].last_landed_at.has_value());
  CHECK_FALSE(inv->sessions[1].last_duration.has_value());
  REQUIRE(inv->landings.size() == 1);
  CHECK(inv->landings[0].duration == 65);
}

TEST_CASE("parse_sessions rejects a non-object document", "[sessions]") {
  const auto doc = nlohmann::json::parse("[1, 2, 3]");
  const auto inv = parse_sessions(doc);
  REQUIRE_FALSE(inv);
  CHECK(inv.error().code == ErrorCode::parse_error);
}

TEST_CASE("landing_key distinguishes session and time", "[sessions]") {
  Landing a{"s1", "", "", 100, 60};
  Landing b{"s1", "", "", 200, 60};  // Same session, later landing.
  Landing c{"s2", "", "", 100, 60};  // Different session, same time.
  CHECK(landing_key(a) != landing_key(b));
  CHECK(landing_key(a) != landing_key(c));
  CHECK(landing_key(a) == landing_key(Landing{"s1", "x", "y", 100, 999}));
}

TEST_CASE("select_new_landings only returns unseen landings", "[sessions]") {
  std::set<std::string> seen;
  const std::vector<Landing> first = {{"s1", "", "", 100, 60},
                                      {"s2", "", "", 100, 90}};

  // First observation: both are new and both are now remembered.
  auto fresh = select_new_landings(first, seen);
  CHECK(fresh.size() == 2);

  // Re-observing the same list yields nothing (client-side dedupe).
  CHECK(select_new_landings(first, seen).empty());

  // A later landing of an existing session is new; an old one stays suppressed.
  const std::vector<Landing> second = {{"s1", "", "", 100, 60},   // seen
                                       {"s1", "", "", 300, 70}};  // new
  auto again = select_new_landings(second, seen);
  REQUIRE(again.size() == 1);
  CHECK(again[0].landed_at == 300);
}
