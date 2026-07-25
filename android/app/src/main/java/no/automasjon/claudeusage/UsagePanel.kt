package no.automasjon.claudeusage

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.RoundRect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import kotlin.math.min

// Same palette/semantics as the desktop tray icon and the widget: green = time
// elapsed through the window ("good"), red = usage ("bad"), the overlap reads
// reddish-orange, and usage outpacing the clock shows red past the green.
private val GreenWindow = Color(60, 200, 90)
private val RedUsage = Color(235, 70, 55)
private val Overlap = Color(200, 96, 62)

@Composable
fun UsagePanel(
    windows: List<NamedWindow>,
    extra: ExtraUsageDto?,
    updatedNote: String,
    nowMs: Long,
) {
  val five = windows.firstOrNull { it.name == "5-hour" }
  val weekly = windows.firstOrNull { it.name == "weekly" }
  val models = windows.filter { it.name.startsWith("weekly:") }

  Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
    Text("Usage", style = MaterialTheme.typography.titleLarge)

    if (five == null && weekly == null) {
      Text("No usage data yet.", style = MaterialTheme.typography.bodyMedium)
    } else {
      weekly?.let { UsageRow("Weekly", it, nowMs) }
      five?.let { UsageRow("5-hour", it, nowMs) }
      models.forEach { model ->
        val label = model.name.removePrefix("weekly:")
            .replaceFirstChar { it.uppercase() } + " (weekly)"
        UsageRow(label, model, nowMs, compact = true)
      }
      extra?.takeIf { it.is_enabled }?.let { eu ->
        val used = eu.used_credits?.toInt() ?: 0
        val limit = eu.monthly_limit?.toInt()
        Text(
            "Extra usage: $used${if (limit != null) " / $limit" else ""} credits",
            style = MaterialTheme.typography.bodyMedium)
      }
      if (updatedNote.isNotBlank()) {
        Text(
            updatedNote,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant)
      }
    }
  }
}

@Composable
private fun UsageRow(label: String, window: NamedWindow, nowMs: Long, compact: Boolean = false) {
  val duration = UsageFormat.durationSecondsFor(window.name)
  val elapsed = UsageFormat.elapsedFraction(window.resetsAt, duration, nowMs)
  val usage = (window.utilization / 100.0).coerceIn(0.0, 1.0).toFloat()
  val trackColor = MaterialTheme.colorScheme.surfaceVariant

  Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween) {
      Text(label, fontWeight = FontWeight.SemiBold, style = MaterialTheme.typography.bodyLarge)
      Text("${window.percent}%", fontWeight = FontWeight.Bold,
          style = MaterialTheme.typography.bodyLarge)
    }
    Canvas(modifier = Modifier.fillMaxWidth().height(if (compact) 14.dp else 24.dp)) {
      val w = size.width
      val h = size.height
      val radius = CornerRadius(h / 2f, h / 2f)
      val clip = Path().apply {
        addRoundRect(RoundRect(0f, 0f, w, h, radius))
      }
      clipPath(clip) {
        drawRect(trackColor, topLeft = Offset.Zero, size = Size(w, h))
        val greenW = elapsed * w
        val redW = usage * w
        val overlap = min(greenW, redW)
        if (overlap > 0f) {
          drawRect(Overlap, topLeft = Offset.Zero, size = Size(overlap, h))
        }
        if (greenW > redW) {
          drawRect(GreenWindow, topLeft = Offset(overlap, 0f), size = Size(greenW - overlap, h))
        } else if (redW > greenW) {
          drawRect(RedUsage, topLeft = Offset(overlap, 0f), size = Size(redW - overlap, h))
        }
      }
    }
    val reset = UsageFormat.countdown(window.resetsAt, nowMs)
    val at = UsageFormat.localReset(window.resetsAt)
    val text = if (at.isBlank()) "Resets in $reset" else "Resets in $reset  ·  $at"
    Text(text, style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant)
  }
}
