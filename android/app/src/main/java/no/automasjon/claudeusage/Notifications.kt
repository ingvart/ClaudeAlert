package no.automasjon.claudeusage

import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.media.AudioAttributes
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat

object Notifications {
  private const val CHANNEL_BASE = "usage_alerts"

  // Android permanently binds a sound to a channel ID at creation time — a
  // channel's sound can NEVER be changed afterward, and deleting + recreating
  // the same ID just restores the original settings. So we derive the channel
  // ID from the chosen sound: picking a new sound yields a new channel that
  // actually uses it. Stale channels are pruned so settings stay tidy.
  private fun channelId(context: Context): String {
    val sound = Prefs.soundUri(context)?.toString() ?: "default"
    return CHANNEL_BASE + "_" + Integer.toHexString(sound.hashCode())
  }

  fun ensureChannel(context: Context) {
    val manager = context.getSystemService(NotificationManager::class.java)
    val id = channelId(context)
    if (manager.getNotificationChannel(id) == null) {
      // IMPORTANCE_HIGH: alerts make a sound and pop as a heads-up.
      val channel = NotificationChannel(id, "Usage alerts", NotificationManager.IMPORTANCE_HIGH)
      channel.description = "Claude usage drops and threshold crossings"
      channel.enableVibration(true)
      Prefs.soundUri(context)?.let { uri ->
        val attributes = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_NOTIFICATION)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build()
        channel.setSound(uri, attributes)
      }
      // A channel with no explicit sound still plays the system default sound.
      manager.createNotificationChannel(channel)
    }
    pruneOldChannels(manager, keep = id)
  }

  // Recreate/select the channel for a freshly-picked sound.
  fun applySound(context: Context) = ensureChannel(context)

  private fun pruneOldChannels(manager: NotificationManager, keep: String) {
    manager.notificationChannels
        .filter { it.id.startsWith(CHANNEL_BASE) && it.id != keep }
        .forEach { manager.deleteNotificationChannel(it.id) }
  }

  fun post(context: Context, id: Int, title: String, text: String) {
    ensureChannel(context)
    val notification = NotificationCompat.Builder(context, channelId(context))
        .setSmallIcon(android.R.drawable.ic_dialog_info)
        .setContentTitle(title)
        .setContentText(text)
        .setPriority(NotificationCompat.PRIORITY_HIGH)
        .setAutoCancel(true)
        .build()
    runCatching { NotificationManagerCompat.from(context).notify(id, notification) }
  }

  // --- Session landings -----------------------------------------------------
  // A separate channel from usage alerts, with its OWN sound (Prefs.sessionSound)
  // so "session done" is audibly distinct from "usage freed". The sound-hash
  // suffix means picking a new sound yields a fresh channel that actually uses it
  // (Android binds a channel's sound permanently at creation — see channelId).
  private const val LANDING_CHANNEL_BASE = "session_landings"

  private fun landingChannelId(context: Context): String {
    val sound = Prefs.sessionSoundUri(context)?.toString() ?: "default"
    return LANDING_CHANNEL_BASE + "_" + Integer.toHexString(sound.hashCode())
  }

  fun ensureLandingChannel(context: Context) {
    val manager = context.getSystemService(NotificationManager::class.java)
    val id = landingChannelId(context)
    if (manager.getNotificationChannel(id) == null) {
      val channel = NotificationChannel(id, "Session landed", NotificationManager.IMPORTANCE_HIGH)
      channel.description = "A Claude Code session finished working and is waiting for you"
      channel.enableVibration(true)
      Prefs.sessionSoundUri(context)?.let { uri ->
        val attributes = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_NOTIFICATION)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build()
        channel.setSound(uri, attributes)
      }
      manager.createNotificationChannel(channel)
    }
    manager.notificationChannels
        .filter { it.id.startsWith(LANDING_CHANNEL_BASE) && it.id != id }
        .forEach { manager.deleteNotificationChannel(it.id) }
  }

  fun postLanding(context: Context, id: Int, label: String, durationSeconds: Long) {
    ensureLandingChannel(context)
    val text = "Finished after ${formatDuration(durationSeconds)} — waiting for you"
    val notification = NotificationCompat.Builder(context, landingChannelId(context))
        .setSmallIcon(android.R.drawable.ic_dialog_info)
        .setContentTitle("✅ $label")
        .setContentText(text)
        .setPriority(NotificationCompat.PRIORITY_HIGH)
        .setAutoCancel(true)
        .build()
    runCatching { NotificationManagerCompat.from(context).notify(id, notification) }
  }
}
