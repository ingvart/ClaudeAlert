#include "net/http_client.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <curl/curl.h>

namespace cusage {

namespace {

// Process-wide curl init/cleanup, performed once on first use.
struct CurlGlobal {
  CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlGlobal() { curl_global_cleanup(); }
};

void ensure_global() { static CurlGlobal global; }

std::size_t write_to_string(char* ptr, std::size_t size, std::size_t nmemb,
                            void* userdata) {
  const std::size_t bytes = size * nmemb;
  static_cast<std::string*>(userdata)->append(ptr, bytes);
  return bytes;
}

using SlistPtr = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;

SlistPtr build_headers(std::span<const std::string> headers) {
  curl_slist* list = nullptr;
  for (const auto& header : headers) {
    list = curl_slist_append(list, header.c_str());
  }
  return SlistPtr(list, &curl_slist_free_all);
}

std::expected<HttpResponse, Error> perform(
    CURL* curl, std::span<const std::string> headers, long timeout_ms) {
  std::string body;
  const SlistPtr header_list = build_headers(headers);

  const long total_ms = timeout_ms > 0 ? timeout_ms : 15000L;
  // Never wait longer to connect than the whole request is allowed to take.
  const long connect_ms = total_ms < 8000L ? total_ms : 8000L;

  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list.get());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_to_string);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, total_ms);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_ms);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "claude-usage-monitor/0.1");

  const CURLcode rc = curl_easy_perform(curl);
  if (rc != CURLE_OK) {
    return std::unexpected(Error{ErrorCode::http_error,
                                 std::string("http request failed: ") +
                                     curl_easy_strerror(rc)});
  }

  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  return HttpResponse{status, std::move(body)};
}

using CurlPtr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;

CurlPtr make_handle() { return CurlPtr(curl_easy_init(), &curl_easy_cleanup); }

}  // namespace

std::expected<HttpResponse, Error> http_get(
    std::string_view url, std::span<const std::string> headers,
    long timeout_ms) {
  ensure_global();
  CurlPtr curl = make_handle();
  if (!curl) {
    return std::unexpected(Error{ErrorCode::http_error, "curl init failed"});
  }
  const std::string url_str(url);
  curl_easy_setopt(curl.get(), CURLOPT_URL, url_str.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1L);
  return perform(curl.get(), headers, timeout_ms);
}

std::expected<HttpResponse, Error> http_post_json(
    std::string_view url, std::string_view json_body,
    std::span<const std::string> headers, long timeout_ms) {
  ensure_global();
  CurlPtr curl = make_handle();
  if (!curl) {
    return std::unexpected(Error{ErrorCode::http_error, "curl init failed"});
  }
  const std::string url_str(url);
  const std::string body(json_body);
  curl_easy_setopt(curl.get(), CURLOPT_URL, url_str.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE,
                   static_cast<long>(body.size()));
  return perform(curl.get(), headers, timeout_ms);
}

}  // namespace cusage
