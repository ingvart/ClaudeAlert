package no.automasjon.claudeusage

enum class AlertKind { DROPPED, THRESHOLD }

data class Alert(val kind: AlertKind, val window: String, val percent: Int, val message: String)

data class AlertConfig(
    val notifyOnDrop: Boolean = true,
    val weeklyThreshold: Int = 80,       // Weekly rising-edge alert %; 0 disables.
    val fiveHourDropFloor: Int = 95,     // 5-hour drop alarms only when prev% >= this.
)

// Faithful port of the C++ compute_alerts. Empty previous → no alerts.
// Per-window rules: weekly windows alarm on any decrease and on a rising
// weekly-threshold crossing; the 5-hour window alarms on a decrease only when its
// previous value was at/above fiveHourDropFloor (and has no rising alert).
// A 5-hour reset is suppressed while the weekly window is fully consumed (100%),
// since a freed 5-hour window is useless when the weekly cap is exhausted.
fun computeAlerts(
    previous: List<NamedWindow>,
    current: List<NamedWindow>,
    config: AlertConfig,
): List<Alert> {
  val previousByName = previous.associateBy { it.name }
  val weeklyConsumed = (current.firstOrNull { it.name == "weekly" }?.percent ?: 0) >= 100
  val alerts = ArrayList<Alert>()
  for (window in current) {
    val before = previousByName[window.name] ?: continue
    val beforePct = before.percent
    val nowPct = window.percent
    val isFiveHour = window.name == "5-hour"

    if (config.notifyOnDrop && nowPct < beforePct) {
      if ((!isFiveHour || beforePct >= config.fiveHourDropFloor) &&
          !(isFiveHour && weeklyConsumed)) {
        alerts.add(Alert(AlertKind.DROPPED, window.name, nowPct,
            "${window.name} freed: $beforePct% -> $nowPct%"))
      }
      continue
    }
    if (!isFiveHour && config.weeklyThreshold > 0 &&
        beforePct < config.weeklyThreshold && nowPct >= config.weeklyThreshold) {
      alerts.add(Alert(AlertKind.THRESHOLD, window.name, nowPct,
          "${window.name} at $nowPct% (>= ${config.weeklyThreshold}%)"))
    }
  }
  return alerts
}
