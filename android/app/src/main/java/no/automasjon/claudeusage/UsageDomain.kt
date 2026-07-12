package no.automasjon.claudeusage

import kotlinx.serialization.Serializable
import kotlin.math.roundToInt

// Wire model for GET /api/oauth/usage. Unknown fields are ignored by the parser.
@Serializable
data class WindowDto(val utilization: Double? = null, val resets_at: String? = null)

@Serializable
data class ExtraUsageDto(
    val is_enabled: Boolean = false,
    val monthly_limit: Double? = null,
    val used_credits: Double? = null,
)

@Serializable
data class UsageDto(
    val five_hour: WindowDto? = null,
    val seven_day: WindowDto? = null,
    val seven_day_sonnet: WindowDto? = null,
    val seven_day_opus: WindowDto? = null,
    val extra_usage: ExtraUsageDto? = null,
)

// Domain view of one window. utilization is already a percentage in [0, 100].
data class NamedWindow(val name: String, val utilization: Double, val resetsAt: String?) {
  val percent: Int get() = utilization.coerceIn(0.0, 100.0).roundToInt()
}

// Flattens the wire model into named windows; a null utilization means the
// window does not apply and is dropped (matches the C++ core).
fun UsageDto.toWindows(): List<NamedWindow> {
  val out = ArrayList<NamedWindow>()
  fun add(name: String, w: WindowDto?) {
    val u = w?.utilization ?: return
    out.add(NamedWindow(name, u, w.resets_at))
  }
  add("5-hour", five_hour)
  add("weekly", seven_day)
  add("weekly:sonnet", seven_day_sonnet)
  add("weekly:opus", seven_day_opus)
  return out
}
