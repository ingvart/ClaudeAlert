#include "core/usage_client.h"

#include <array>
#include <string>
#include <string_view>

#include "core/json_parse.h"
#include "net/http_client.h"

namespace cusage {

namespace {

constexpr std::string_view kUsageUrl =
    "https://api.anthropic.com/api/oauth/usage";

}  // namespace

std::expected<UsageSnapshot, Error> fetch_usage(
    const std::string& access_token) {
  const std::array<std::string, 3> headers = {
      "Authorization: Bearer " + access_token,
      std::string("anthropic-beta: oauth-2025-04-20"),
      std::string("Content-Type: application/json"),
  };

  auto response = http_get(kUsageUrl, headers);
  if (!response) return std::unexpected(response.error());

  if (response->status_ == 401 || response->status_ == 403) {
    return std::unexpected(Error{
        ErrorCode::auth_error,
        "usage endpoint rejected token (HTTP " +
            std::to_string(response->status_) + ")"});
  }
  if (response->status_ != 200) {
    // Body is safe to surface (no secrets) and aids diagnosis of e.g. a plan
    // that the endpoint does not serve.
    return std::unexpected(Error{
        ErrorCode::http_error,
        "usage endpoint HTTP " + std::to_string(response->status_) + ": " +
            response->body_.substr(0, 300)});
  }

  auto doc = parse_json(response->body_);
  if (!doc) return std::unexpected(doc.error());
  return parse_usage(*doc);
}

}  // namespace cusage
