#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/error.h"

namespace cusage {

// The OAuth bundle Claude Code stores under "claudeAiOauth" in
// ~/.claude/.credentials.json. Secrets are held only as long as needed and must
// never be logged.
struct OAuthTokens {
  std::string access_token_;
  std::optional<std::string> refresh_token_;
  std::optional<std::string> expires_at_;        // Epoch ms as stored (string).
  std::optional<std::string> subscription_type_;
  std::vector<std::string> scopes_;
};

// Default credentials-file location for the current user.
std::expected<std::filesystem::path, Error> default_credentials_path();

// Reads and decodes the OAuth tokens from `path`.
std::expected<OAuthTokens, Error> read_oauth_tokens(
    const std::filesystem::path& path);

}  // namespace cusage
