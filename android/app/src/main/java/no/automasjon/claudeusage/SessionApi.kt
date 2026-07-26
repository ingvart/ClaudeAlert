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

  // Throws SessionHttpException on non-2xx (except 204), IOException on transport
  // failure. With `since` set, a conditional poll: the relay replies 204 (and this
  // returns null) when nothing has changed since that revision, saving data.
  fun fetch(relayUrl: String, relayToken: String, since: Long? = null): SessionInventory? {
    var url = relayBase(relayUrl) + "/sessions"
    if (since != null) url += "?since=$since"
    val builder = Request.Builder()
        .url(url)
        .header("Content-Type", "application/json")
        .get()
    if (relayToken.isNotBlank()) builder.header("Authorization", "Bearer $relayToken")
    client.newCall(builder.build()).execute().use { response ->
      if (response.code == 204) return null  // Unchanged since `since`.
      val body = response.body?.string().orEmpty()
      if (!response.isSuccessful) throw SessionHttpException(response.code)
      return json.decodeFromString(SessionsDto.serializer(), body).toInventory()
    }
  }
}
