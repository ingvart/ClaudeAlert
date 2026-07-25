package no.automasjon.claudeusage

import kotlinx.serialization.json.Json
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.IOException
import java.util.concurrent.TimeUnit

// Carries the HTTP status so callers can distinguish a rejected token (401/403)
// from a transient failure.
class SessionHttpException(val status: Int) : IOException("sessions HTTP $status")

// Reads the session inventory from the self-hosted relay's GET /sessions. This
// is the phone's ONLY relay dependency — usage is still fetched directly from
// Anthropic. Mirrors the C++ fetch_sessions (net/session_client.cpp).
object SessionApi {
  // LAN call on the poll cadence: a modest timeout keeps a briefly unreachable
  // relay from stalling the worker.
  private val client = OkHttpClient.Builder().callTimeout(8, TimeUnit.SECONDS).build()
  private val json = Json { ignoreUnknownKeys = true }

  // Throws SessionHttpException on non-2xx, IOException on transport failure.
  fun fetch(relayUrl: String, relayToken: String): SessionInventory {
    val url = relayBase(relayUrl) + "/sessions"
    val builder = Request.Builder()
        .url(url)
        .header("Content-Type", "application/json")
        .get()
    if (relayToken.isNotBlank()) builder.header("Authorization", "Bearer $relayToken")
    client.newCall(builder.build()).execute().use { response ->
      val body = response.body?.string().orEmpty()
      if (!response.isSuccessful) throw SessionHttpException(response.code)
      return json.decodeFromString(SessionsDto.serializer(), body).toInventory()
    }
  }
}
