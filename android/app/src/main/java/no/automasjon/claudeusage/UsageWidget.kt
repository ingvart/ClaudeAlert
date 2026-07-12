package no.automasjon.claudeusage

import android.content.Context
import android.content.Intent
import androidx.glance.GlanceId
import androidx.glance.GlanceModifier
import androidx.glance.Image
import androidx.glance.ImageProvider
import androidx.glance.action.clickable
import androidx.glance.appwidget.GlanceAppWidget
import androidx.glance.appwidget.GlanceAppWidgetReceiver
import androidx.glance.appwidget.action.actionStartActivity
import androidx.glance.appwidget.provideContent
import androidx.glance.appwidget.updateAll
import androidx.glance.layout.Box
import androidx.glance.layout.ContentScale
import androidx.glance.layout.fillMaxSize

// 1x1 home-screen widget showing the same two-column graphic as the desktop
// tray icon, rendered from the latest stored snapshot. Tapping it opens the
// config/settings screen.
class UsageWidget : GlanceAppWidget() {
  override suspend fun provideGlance(context: Context, id: GlanceId) {
    val windows = Prefs.lastSnapshot(context)
    val bitmap = IconRenderer.render(windows, 144)
    val configIntent = Intent(context, MainActivity::class.java)
    provideContent {
      Box(modifier = GlanceModifier.fillMaxSize().clickable(actionStartActivity(configIntent))) {
        Image(
            provider = ImageProvider(bitmap),
            contentDescription = "Claude usage",
            modifier = GlanceModifier.fillMaxSize(),
            contentScale = ContentScale.Fit)
      }
    }
  }

  companion object {
    suspend fun updateAll(context: Context) = UsageWidget().updateAll(context)
  }
}

class UsageWidgetReceiver : GlanceAppWidgetReceiver() {
  override val glanceAppWidget: GlanceAppWidget = UsageWidget()
}
