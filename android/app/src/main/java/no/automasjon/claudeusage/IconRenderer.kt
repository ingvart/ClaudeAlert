package no.automasjon.claudeusage

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import java.time.OffsetDateTime

// Renders the same two-column glyph as the desktop tray icon: left = 5-hour,
// right = weekly. A solid green bar is window progress (time elapsed, "good");
// a translucent red bar on top is usage ("bad"), so the overlap reads orange and
// usage outpacing the clock shows red poking above the green.
object IconRenderer {
  private const val FIVE_HOUR_SECONDS = 5L * 3600
  private const val WEEKLY_SECONDS = 7L * 24 * 3600

  fun render(windows: List<NamedWindow>, sizePx: Int): Bitmap {
    val bitmap = Bitmap.createBitmap(sizePx, sizePx, Bitmap.Config.ARGB_8888)
    val canvas = Canvas(bitmap)
    canvas.drawColor(Color.BLACK)

    val five = windows.firstOrNull { it.name == "5-hour" }
    val weekly = windows.firstOrNull { it.name == "weekly" }
    // Weekly (60% width) is wider than 5-hour (40%) since it matters more.
    drawColumn(canvas, sizePx, 0.08f, 0.40f, five, FIVE_HOUR_SECONDS)
    drawColumn(canvas, sizePx, 0.44f, 0.92f, weekly, WEEKLY_SECONDS)
    drawLabel(canvas, sizePx, 0.24f, five)
    drawLabel(canvas, sizePx, 0.68f, weekly)
    return bitmap
  }

  private val greenPaint = Paint().apply { color = Color.rgb(60, 200, 90) }     // window progress
  private val usagePaint = Paint().apply { color = Color.rgb(235, 70, 55) }     // usage above the elapsed line: solid red
  private val overlapPaint = Paint().apply { color = Color.rgb(200, 96, 62) }   // usage within the elapsed window: reddish-orange
  private val textPaint = Paint().apply {
    color = Color.WHITE
    isAntiAlias = true
    textAlign = Paint.Align.CENTER
  }

  // Percent label near the top of a column so the numbers are readable even at
  // small sizes (and confirm the widget has data).
  private fun drawLabel(canvas: Canvas, size: Int, centerXFrac: Float, window: NamedWindow?) {
    textPaint.textSize = size * 0.18f
    val label = if (window == null) "-" else "${window.percent}%"
    canvas.drawText(label, centerXFrac * size, size * 0.22f, textPaint)
  }

  private fun drawColumn(
      canvas: Canvas, size: Int, x0Frac: Float, x1Frac: Float,
      window: NamedWindow?, durationSeconds: Long) {
    val x0 = x0Frac * size
    val x1 = x1Frac * size
    val top = size * 0.08f
    val bottom = size * 0.92f
    val span = bottom - top

    val elapsed = elapsedFraction(window, durationSeconds)
    val usage = ((window?.utilization ?: 0.0) / 100.0).coerceIn(0.0, 1.0)
    val greenHeight = (elapsed * span).toFloat()
    val redHeight = (usage * span).toFloat()
    val overlap = minOf(greenHeight, redHeight)

    // Overlap (usage within the elapsed window) = reddish-orange.
    if (overlap > 0f) canvas.drawRect(x0, bottom - overlap, x1, bottom, overlapPaint)
    // The taller bar's exposed top: green if the window is ahead, solid red if
    // usage is ahead.
    if (greenHeight > redHeight) {
      canvas.drawRect(x0, bottom - greenHeight, x1, bottom - overlap, greenPaint)
    } else if (redHeight > greenHeight) {
      canvas.drawRect(x0, bottom - redHeight, x1, bottom - overlap, usagePaint)
    }
  }

  private fun elapsedFraction(window: NamedWindow?, durationSeconds: Long): Double {
    val resetEpoch = resetEpochSeconds(window?.resetsAt) ?: return 0.0
    val remaining = resetEpoch - (System.currentTimeMillis() / 1000)
    return (1.0 - remaining.toDouble() / durationSeconds.toDouble()).coerceIn(0.0, 1.0)
  }

  private fun resetEpochSeconds(text: String?): Long? {
    if (text == null) return null
    return runCatching { OffsetDateTime.parse(text).toEpochSecond() }.getOrNull()
  }
}
