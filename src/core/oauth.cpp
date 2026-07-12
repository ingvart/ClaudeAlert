#include "core/oauth.h"

#include <array>
#include <charconv>
#include <chrono>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "core/json_parse.h"
#include "net/http_client.h"

namespace cusage {

namespace {

constexpr std::string_view kTokenUrl =
    "https://platform.claude.com/v1/oauth/token";
// The public (PKCE) client id the official CLI uses for token exchange.
constexpr std::string_view kClientId = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";

long long now_epoch_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string join_scopes(const std::vector<std::string>& scopes) {
  std::string joined;
  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (i != 0) joined += ' ';
    joined += scopes[i];
  }
  return joined;
}

}  // namespace

std::expected<OAuthTokens, Error> refresh_oauth_token(
    const OAuthTokens& current) {
  if (!current.refresh_token_) {
    return std::unexpected(
        Error{ErrorCode::auth_error, "no refresh token available"});
  }

  nlohmann::json request = {
      {"grant_type", "refresh_token"},
      {"refresh_token", *current.refresh_token_},
      {"client_id", kClientId},
  };
  if (!current.scopes_.empty()) {
    request["scope"] = join_scopes(current.scopes_);
  }

  const std::array<std::string, 1> headers = {
      std::string("Content-Type: application/json")};
  auto response = http_post_json(kTokenUrl, request.dump(), headers);
  if (!response) return std::unexpected(response.error());
  if (response->status_ != 200) {
    return std::unexpected(Error{
        ErrorCode::auth_error,
        "token refresh failed (HTTP " + std::to_string(response->status_) + ")"});
  }

  auto doc = parse_json(response->body_);
  if (!doc) return std::unexpected(doc.error());

  const auto access = doc->find("access_token");
  if (access == doc->end() || !access->is_string()) {
    return std::unexpected(
        Error{ErrorCode::auth_error, "refresh response missing access_token"});
  }

  OAuthTokens refreshed = current;
  refreshed.access_token_ = access->get<std::string>();
  if (const auto it = doc->find("refresh_token");
      it != doc->end() && it->is_string()) {
    refreshed.refresh_token_ = it->get<std::string>();
  }
  if (const auto it = doc->find("expires_in");
      it != doc->end() && it->is_number()) {
    const long long expiry_ms =
        now_epoch_ms() + static_cast<long long>(it->get<double>() * 1000.0);
    refreshed.expires_at_ = std::to_string(expiry_ms);
  }
  if (const auto it = doc->find("scope");
      it != doc->end() && it->is_string()) {
    refreshed.scopes_.clear();
    std::string_view rest = it->get<std::string>();
    while (!rest.empty()) {
      const auto space = rest.find(' ');
      refreshed.scopes_.emplace_back(rest.substr(0, space));
      if (space == std::string_view::npos) break;
      rest.remove_prefix(space + 1);
    }
  }
  return refreshed;
}

bool token_expiring_soon(const OAuthTokens& tokens, long long skew_seconds) {
  if (!tokens.expires_at_) return false;
  const std::string& value = *tokens.expires_at_;
  long long expiry_ms = 0;
  const auto [ptr, ec] =
      std::from_chars(value.data(), value.data() + value.size(), expiry_ms);
  if (ec != std::errc()) return false;
  return now_epoch_ms() + skew_seconds * 1000 >= expiry_ms;
}

}  // namespace cusage
