package no.automasjon.claudeusage

import kotlinx.serialization.json.Json
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.IOException
import java.util.concurrent.TimeUnit

// Carries the HTTP status so callers can distinguish rejection (401/403) from a
// transient rate limit (429).
class UsageHttpException(val status: Int, val retryAfterSeconds: Long = 0) :
    IOException("usage HTTP $status")

// Mirrors the C++ usage_client: a read-only GET with the OAuth bearer token.
object UsageApi {
  private const val URL = "https://api.anthropic.com/api/oauth/usage"
  private val client = OkHttpClient.Builder().callTimeout(15, TimeUnit.SECONDS).build()
  private val json = Json { ignoreUnknownKeys = true }

  // Throws IOException on transport or non-2xx; the caller decides retry policy.
  fun fetch(token: String): UsageDto {
    val request = Request.Builder()
        .url(URL)
        .header("Authorization", "Bearer $token")
        .header("anthropic-beta", "oauth-2025-04-20")
        .header("Content-Type", "application/json")
        .header("User-Agent", OAuth.USER_AGENT)
        .get()
        .build()
    client.newCall(request).execute().use { response ->
      val body = response.body?.string().orEmpty()
      if (!response.isSuccessful) {
        val retryAfter = response.header("Retry-After")?.toLongOrNull() ?: 0L
        throw UsageHttpException(response.code, retryAfter)
      }
      return json.decodeFromString(UsageDto.serializer(), body)
    }
  }
}
