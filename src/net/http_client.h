#pragma once

#include <expected>
#include <span>
#include <string>
#include <string_view>

#include "core/error.h"

namespace cusage {

struct HttpResponse {
  long status_ = 0;
  std::string body_;
};

// Boundary over libcurl (a C API — it does not throw, so no try/catch). Each
// header is a full "Name: value" line. The body is returned for any status; the
// caller inspects `status_`. Non-2xx is not itself an error here.
//
// `timeout_ms` caps the whole request; 0 means the default (15s). A short value
// suits fire-and-forget callers (e.g. a hook) that must not linger when the peer
// is unreachable.
std::expected<HttpResponse, Error> http_get(std::string_view url,
                                            std::span<const std::string> headers,
                                            long timeout_ms = 0);

std::expected<HttpResponse, Error> http_post_json(
    std::string_view url, std::string_view json_body,
    std::span<const std::string> headers, long timeout_ms = 0);

}  // namespace cusage
