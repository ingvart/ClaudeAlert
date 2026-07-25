package no.automasjon.claudeusage

import java.time.OffsetDateTime
import java.time.ZoneId
import java.time.format.DateTimeFormatter

// Time helpers shared by the settings usage panel (and mirroring IconRenderer's
// window-progress math). "Elapsed" is how far through the window the clock is;
// "usage" is the reported utilization — the bar shows both.
object UsageFormat {
  private val localFmt = DateTimeFormatter.ofPattern("EEE HH:mm")

  const val FIVE_HOUR_SECONDS = 5L * 3600
  const val WEEKLY_SECONDS = 7L * 24 * 3600

  fun durationSecondsFor(name: String): Long =
      if (name == "5-hour") FIVE_HOUR_SECONDS else WEEKLY_SECONDS

  private fun resetEpochSeconds(text: String?): Long? {
    if (text == null) return null
    return runCatching { OffsetDateTime.parse(text).toEpochSecond() }.getOrNull()
  }

  // Fraction of the window's time already elapsed, 0..1.
  fun elapsedFraction(resetsAt: String?, durationSeconds: Long, nowMs: Long): Float {
    val reset = resetEpochSeconds(resetsAt) ?: return 0f
    val remaining = reset - nowMs / 1000
    return (1.0 - remaining.toDouble() / durationSeconds).coerceIn(0.0, 1.0).toFloat()
  }

  // "2d 3h" / "3h 12m" / "12m" until reset; "now" once passed.
  fun countdown(resetsAt: String?, nowMs: Long): String {
    val reset = resetEpochSeconds(resetsAt) ?: return "—"
    var secs = reset - nowMs / 1000
    if (secs <= 0) return "now"
    val days = secs / 86400; secs %= 86400
    val hours = secs / 3600; secs %= 3600
    val mins = secs / 60
    return when {
      days > 0 -> "${days}d ${hours}h"
      hours > 0 -> "${hours}h ${mins}m"
      else -> "${mins}m"
    }
  }

  // Local wall-clock time of the reset, e.g. "Sat 14:59".
  fun localReset(resetsAt: String?): String {
    if (resetsAt == null) return ""
    return runCatching {
      OffsetDateTime.parse(resetsAt).atZoneSameInstant(ZoneId.systemDefault()).format(localFmt)
    }.getOrDefault("")
  }
}
