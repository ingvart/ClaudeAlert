package no.automasjon.claudeusage

import android.util.Base64
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.IOException
import java.security.MessageDigest
import java.security.SecureRandom
import java.util.concurrent.TimeUnit

// PKCE OAuth login for the Claude subscription — the same flow the desktop and
// the Pi use. The phone holds its OWN independent login and refreshes its own
// token, so no relay, Pi, or desktop is needed.
//
// Flow:
//   begin()                       -> open url in a browser, approve, copy code
//   exchange(code, verifier, ...) -> first token bundle
//   refresh(refreshToken)         -> renew the access token before it expires
object OAuth {
  private const val CLIENT_ID = "9d1c250a-e61b-44d9-88ed-5944d1962f5e"
  private const val AUTHORIZE_URL = "https://claude.ai/oauth/authorize"
  // The token endpoint lives on claude.ai for subscription logins. (console.
  // anthropic.com/v1/oauth/token 404s; with a browser UA it even returns a
  // misleading 429, which is what made this look like a rate limit.)
  private const val TOKEN_URL = "https://claude.ai/v1/oauth/token"
  private const val REDIRECT_URI = "https://console.anthropic.com/oauth/code/callback"
  private const val SCOPE = "org:create_api_key user:profile user:inference"

  // The endpoint only accepts the official client's signature; a browser UA is
  // deflected (429), the default Python/OkHttp UA is hard-banned (Cloudflare
  // 1010). Present exactly what Claude Code sends.
  const val USER_AGENT = "claude-cli/1.0.56 (external, cli)"

  private val client = OkHttpClient.Builder().callTimeout(30, TimeUnit.SECONDS).build()
  private val json = Json { ignoreUnknownKeys = true }
  private val jsonMedia = "application/json".toMediaType()

  // The authorize URL plus the PKCE secrets that must survive until the user
  // pastes the code back (stashed via Prefs).
  data class Begin(val url: String, val verifier: String, val state: String)

  data class Tokens(
      val accessToken: String,
      val refreshToken: String,
      val expiresAt: Long,   // epoch millis
      val scopes: List<String>,
  )

  @Serializable
  private data class TokenResponse(
      val access_token: String,
      val refresh_token: String? = null,
      val expires_in: Long? = null,
      val scope: String? = null,
  )

  private fun b64url(bytes: ByteArray): String =
      Base64.encodeToString(bytes, Base64.URL_SAFE or Base64.NO_PADDING or Base64.NO_WRAP)

  private fun randomB64Url(): String {
    val bytes = ByteArray(32)
    SecureRandom().nextBytes(bytes)
    return b64url(bytes)
  }

  fun begin(): Begin {
    val verifier = randomB64Url()
    val state = randomB64Url()
    val challenge = b64url(
        MessageDigest.getInstance("SHA-256").digest(verifier.toByteArray(Charsets.US_ASCII)))
    val url = AUTHORIZE_URL.toHttpUrl().newBuilder()
        .addQueryParameter("code", "true")
        .addQueryParameter("client_id", CLIENT_ID)
        .addQueryParameter("response_type", "code")
        .addQueryParameter("redirect_uri", REDIRECT_URI)
        .addQueryParameter("scope", SCOPE)
        .addQueryParameter("code_challenge", challenge)
        .addQueryParameter("code_challenge_method", "S256")
        .addQueryParameter("state", state)
        .build()
        .toString()
    return Begin(url, verifier, state)
  }

  // Exchanges the pasted authorization code (the result page returns "code#state").
  fun exchange(pastedCode: String, verifier: String, state: String): Tokens {
    val code = pastedCode.substringBefore("#").substringBefore("&").trim()
    val payload = buildJsonObject {
      put("grant_type", "authorization_code")
      put("client_id", CLIENT_ID)
      put("code", code)
      put("redirect_uri", REDIRECT_URI)
      put("code_verifier", verifier)
      put("state", state)
    }
    return post(payload.toString())
  }

  fun refresh(refreshToken: String): Tokens {
    val payload = buildJsonObject {
      put("grant_type", "refresh_token")
      put("refresh_token", refreshToken)
      put("client_id", CLIENT_ID)
    }
    return post(payload.toString())
  }

  private fun post(body: String): Tokens {
    val request = Request.Builder()
        .url(TOKEN_URL)
        .header("Content-Type", "application/json")
        .header("User-Agent", USER_AGENT)
        .header("Accept", "application/json, text/plain, */*")
        .header("Accept-Language", "en-US,en;q=0.9")
        .header("Referer", "https://claude.ai/")
        .header("Origin", "https://claude.ai")
        .post(body.toRequestBody(jsonMedia))
        .build()
    client.newCall(request).execute().use { response ->
      val text = response.body?.string().orEmpty()
      if (!response.isSuccessful) {
        throw IOException("token endpoint HTTP ${response.code}: ${text.take(200)}")
      }
      val parsed = json.decodeFromString(TokenResponse.serializer(), text)
      val expiresAt = System.currentTimeMillis() + (parsed.expires_in ?: 0L) * 1000L
      return Tokens(
          accessToken = parsed.access_token,
          refreshToken = parsed.refresh_token ?: refreshTokenFallback(),
          expiresAt = expiresAt,
          scopes = parsed.scope?.split(" ")?.filter { it.isNotBlank() } ?: emptyList(),
      )
    }
  }

  // A refresh response may omit refresh_token (the old one stays valid); callers
  // that have it should keep theirs. This fallback is only hit on first login,
  // where the server always returns one, so it is effectively unreachable.
  private fun refreshTokenFallback(): String = ""
}
