package no.automasjon.claudeusage

import android.content.Context

// Hands out a currently-valid access token, refreshing it silently when it is
// near expiry. You log in ONCE; from then on the app renews the short-lived
// access token itself using the long-lived refresh token. Callers never deal
// with expiry.
object TokenManager {
  private const val SKEW_MS = 5 * 60 * 1000L  // refresh 5 min before expiry

  class NotLoggedInException : Exception("not logged in")

  // Returns a valid access token, refreshing (and persisting) if needed.
  // Throws NotLoggedInException if there is no login yet; propagates network
  // errors from the refresh so the caller can retry later.
  fun validAccessToken(context: Context): String {
    if (!Prefs.isLoggedIn(context)) throw NotLoggedInException()
    val expiresAt = Prefs.expiresAt(context)
    val token = Prefs.accessToken(context)
    if (token.isNotBlank() && expiresAt != 0L &&
        System.currentTimeMillis() + SKEW_MS < expiresAt) {
      return token
    }
    val refreshed = OAuth.refresh(Prefs.refreshToken(context))
    Prefs.saveTokens(context, refreshed)
    return refreshed.accessToken
  }
}
