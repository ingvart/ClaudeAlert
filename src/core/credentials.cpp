#include "core/credentials.h"

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "core/json_parse.h"
#include "platform/user_paths.h"

namespace cusage {

namespace {

// expiresAt may be stored as an ISO string or an epoch number depending on the
// client version; normalize either to a string for downstream parsing.
std::optional<std::string> read_string_or_number(const nlohmann::json& parent,
                                                  std::string_view key) {
  const auto it = parent.find(std::string(key));
  if (it == parent.end()) return std::nullopt;
  if (it->is_string()) return it->get<std::string>();
  if (it->is_number_integer()) return std::to_string(it->get<long long>());
  if (it->is_number()) return std::to_string(it->get<double>());
  return std::nullopt;
}

}  // namespace

std::expected<std::filesystem::path, Error> default_credentials_path() {
  auto dir = platform::claude_config_dir();
  if (!dir) return std::unexpected(dir.error());
  return *dir / ".credentials.json";
}

std::expected<OAuthTokens, Error> read_oauth_tokens(
    const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return std::unexpected(Error{
        ErrorCode::io_error, "cannot open credentials file: " + path.string()});
  }
  const std::string contents((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());

  auto doc = parse_json(contents);
  if (!doc) return std::unexpected(doc.error());

  const auto oauth = doc->find("claudeAiOauth");
  if (oauth == doc->end() || !oauth->is_object()) {
    return std::unexpected(Error{ErrorCode::auth_error,
                                 "no claudeAiOauth object in credentials"});
  }

  const auto access = oauth->find("accessToken");
  if (access == oauth->end() || !access->is_string()) {
    return std::unexpected(
        Error{ErrorCode::auth_error, "no accessToken in credentials"});
  }

  OAuthTokens tokens;
  tokens.access_token_ = access->get<std::string>();
  if (const auto it = oauth->find("refreshToken");
      it != oauth->end() && it->is_string()) {
    tokens.refresh_token_ = it->get<std::string>();
  }
  tokens.expires_at_ = read_string_or_number(*oauth, "expiresAt");
  tokens.subscription_type_ = read_string_or_number(*oauth, "subscriptionType");
  if (const auto it = oauth->find("scopes");
      it != oauth->end() && it->is_array()) {
    for (const auto& scope : *it) {
      if (scope.is_string()) tokens.scopes_.push_back(scope.get<std::string>());
    }
  }
  return tokens;
}

}  // namespace cusage
