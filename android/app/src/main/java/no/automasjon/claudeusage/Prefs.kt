package no.automasjon.claudeusage

import android.content.Context
import android.net.Uri
import kotlinx.serialization.Serializable
import kotlinx.serialization.builtins.ListSerializer
import kotlinx.serialization.json.Json
import java.io.File

@Serializable
data class StoredWindow(val name: String, val utilization: Double, val resetsAt: String? = null)

// Persists the phone's OWN Claude login (access + refresh token), the alert
// config, the previous snapshot (for diffing across worker runs), and the chosen
// notification sound. The phone logs in once and refreshes its token itself; no
// relay, Pi, or desktop is involved.
object Prefs {
  private const val FILE = "cusage"
  private const val K_ACCESS = "access_token"
  private const val K_REFRESH = "refresh_token"
  private const val K_EXPIRES = "expires_at"
  private const val K_SCOPES = "scopes"
  private const val K_PKCE_VERIFIER = "pkce_verifier"
  private const val K_PKCE_STATE = "pkce_state"
  private const val K_WEEKLY = "weekly_threshold"
  private const val K_DROP = "notify_on_drop"
  private const val K_FIVE_FLOOR = "five_hour_drop_floor"
  private const val K_POLL = "poll_minutes"
  private const val K_SNAPSHOT = "last_snapshot"
  private const val K_SOUND = "sound_uri"
  private val json = Json { ignoreUnknownKeys = true }

  private fun prefs(context: Context) =
      context.getSharedPreferences(FILE, Context.MODE_PRIVATE)

  // --- Login / tokens -------------------------------------------------------

  fun isLoggedIn(context: Context): Boolean =
      prefs(context).getString(K_REFRESH, "").orEmpty().isNotBlank()

  fun accessToken(context: Context): String =
      prefs(context).getString(K_ACCESS, "").orEmpty()

  fun refreshToken(context: Context): String =
      prefs(context).getString(K_REFRESH, "").orEmpty()

  fun expiresAt(context: Context): Long = prefs(context).getLong(K_EXPIRES, 0L)

  fun saveTokens(context: Context, tokens: OAuth.Tokens) {
    val editor = prefs(context).edit()
        .putString(K_ACCESS, tokens.accessToken)
        .putLong(K_EXPIRES, tokens.expiresAt)
        .putString(K_SCOPES, tokens.scopes.joinToString(" "))
    // A refresh response may omit the refresh token; keep the existing one then.
    if (tokens.refreshToken.isNotBlank()) editor.putString(K_REFRESH, tokens.refreshToken)
    editor.apply()
  }

  fun logout(context: Context) {
    prefs(context).edit()
        .remove(K_ACCESS).remove(K_REFRESH).remove(K_EXPIRES).remove(K_SCOPES)
        .remove(K_SNAPSHOT)
        .apply()
  }

  fun setPendingPkce(context: Context, verifier: String, state: String) =
      prefs(context).edit().putString(K_PKCE_VERIFIER, verifier)
          .putString(K_PKCE_STATE, state).apply()

  fun pendingVerifier(context: Context): String =
      prefs(context).getString(K_PKCE_VERIFIER, "").orEmpty()

  fun pendingState(context: Context): String =
      prefs(context).getString(K_PKCE_STATE, "").orEmpty()

  // --- Config / snapshot / sound -------------------------------------------

  fun pollMinutes(context: Context): Long =
      prefs(context).getInt(K_POLL, 15).coerceAtLeast(15).toLong()

  fun config(context: Context): AlertConfig {
    val p = prefs(context)
    var config = AlertConfig(
        notifyOnDrop = p.getBoolean(K_DROP, true),
        weeklyThreshold = p.getInt(K_WEEKLY, 80),
        fiveHourDropFloor = p.getInt(K_FIVE_FLOOR, 95),
    )
    val file = File(context.getExternalFilesDir(null), "cusage.conf")
    if (file.exists()) config = applyFileOverride(file.readText(), config)
    return config
  }

  fun setConfig(context: Context, config: AlertConfig) {
    prefs(context).edit()
        .putBoolean(K_DROP, config.notifyOnDrop)
        .putInt(K_WEEKLY, config.weeklyThreshold)
        .putInt(K_FIVE_FLOOR, config.fiveHourDropFloor)
        .apply()
  }

  fun soundUri(context: Context): Uri? =
      prefs(context).getString(K_SOUND, null)?.let { Uri.parse(it) }

  fun setSoundUri(context: Context, uri: Uri?) =
      prefs(context).edit().putString(K_SOUND, uri?.toString()).apply()

  fun lastSnapshot(context: Context): List<NamedWindow> {
    val raw = prefs(context).getString(K_SNAPSHOT, null) ?: return emptyList()
    return runCatching {
      json.decodeFromString(ListSerializer(StoredWindow.serializer()), raw)
          .map { NamedWindow(it.name, it.utilization, it.resetsAt) }
    }.getOrDefault(emptyList())
  }

  fun setLastSnapshot(context: Context, windows: List<NamedWindow>) {
    val stored = windows.map { StoredWindow(it.name, it.utilization, it.resetsAt) }
    val raw = json.encodeToString(ListSerializer(StoredWindow.serializer()), stored)
    prefs(context).edit().putString(K_SNAPSHOT, raw).apply()
  }

  private fun applyFileOverride(text: String, base: AlertConfig): AlertConfig {
    var config = base
    for (rawLine in text.lines()) {
      val line = rawLine.trim()
      if (line.isEmpty() || line.startsWith("#")) continue
      val eq = line.indexOf('=')
      if (eq < 0) continue
      val key = line.substring(0, eq).trim()
      val value = line.substring(eq + 1).trim()
      config = when (key) {
        "notify_on_drop" -> config.copy(notifyOnDrop = value.toBooleanLenient(config.notifyOnDrop))
        "weekly_threshold" -> config.copy(weeklyThreshold = value.toIntOrNull() ?: config.weeklyThreshold)
        "five_hour_drop_floor" -> config.copy(fiveHourDropFloor = value.toIntOrNull() ?: config.fiveHourDropFloor)
        else -> config
      }
    }
    return config
  }

  private fun String.toBooleanLenient(fallback: Boolean): Boolean = when (lowercase()) {
    "true", "1", "yes", "on" -> true
    "false", "0", "no", "off" -> false
    else -> fallback
  }
}
