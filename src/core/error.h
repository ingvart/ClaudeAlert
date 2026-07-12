#pragma once

#include <string>
#include <string_view>

namespace cusage {

enum class ErrorCode {
  io_error,
  parse_error,
  auth_error,
  http_error,
  not_subscriber,
  unexpected,
};

// A recoverable, expected failure. `message` carries human-readable context for
// the caller to log. It must never contain secrets (e.g. a bearer token).
struct Error {
  ErrorCode code;
  std::string message;
};

constexpr std::string_view error_code_name(ErrorCode code) {
  switch (code) {
    case ErrorCode::io_error:
      return "io_error";
    case ErrorCode::parse_error:
      return "parse_error";
    case ErrorCode::auth_error:
      return "auth_error";
    case ErrorCode::http_error:
      return "http_error";
    case ErrorCode::not_subscriber:
      return "not_subscriber";
    case ErrorCode::unexpected:
      return "unexpected";
  }
  return "unknown";
}

}  // namespace cusage
