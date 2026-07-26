package no.automasjon.claudeusage

import android.content.Context
import androidx.work.Constraints
import androidx.work.CoroutineWorker
import androidx.work.ExistingWorkPolicy
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.WorkerParameters
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.concurrent.TimeUnit

// Fetch the relay's session inventory and notify on any landing not seen before.
// Seeds silently on the first poll (or after a relay change) so pre-existing
// landings never fire. Shared by the fast session poller. Throws on network
// error; callers swallow it (the next poll catches up).
fun pollSessionsOnce(context: Context) {
  val relayUrl = Prefs.relayUrl(context)
  if (relayUrl.isBlank()) return  // Feature off.

  val inventory = SessionApi.fetch(relayUrl, Prefs.relayToken(context))
  val seen = Prefs.seenLandings(context)

  if (!Prefs.landingsSeeded(context)) {
    inventory.landings.forEach { seen.add(landingKey(it)) }
    Prefs.setSeenLandings(context, seen)
    Prefs.setLandingsSeeded(context, true)
    return
  }

  val fresh = selectNewLandings(inventory.landings, seen)
  var notificationId = 2000
  for (landing in fresh) {
    Notifications.postLanding(
        context, notificationId++,
        sessionLabel(landing.title, landing.cwd), landing.duration)
  }
  Prefs.setSeenLandings(context, seen)
}

// Session landings need a much faster cadence than usage (which stays on the
// 15-min periodic worker). WorkManager's *periodic* work floors at 15 min, so we
// chain a *one-time* worker that re-enqueues itself ~1 min later. The 15-min
// usage worker also re-arms this chain (ensureScheduled) as a keepalive in case
// the chain ever dies (process death, etc.); WorkManager restores periodic work
// across reboots, so the chain always gets revived within 15 min.
class SessionPollWorker(appContext: Context, params: WorkerParameters) :
    CoroutineWorker(appContext, params) {

  override suspend fun doWork(): Result = withContext(Dispatchers.IO) {
    val context = applicationContext
    try {
      pollSessionsOnce(context)
    } catch (e: Exception) {
      // Relay blip / off-LAN / transient — ignore; the next tick catches up.
    }
    // Re-arm the next tick only while the feature is configured.
    if (Prefs.relayUrl(context).isNotBlank()) schedule(context, 1)
    Result.success()
  }

  companion object {
    private const val NAME = "session_poll"

    private fun request(delayMinutes: Long) =
        OneTimeWorkRequestBuilder<SessionPollWorker>()
            .setInitialDelay(delayMinutes, TimeUnit.MINUTES)
            .setConstraints(
                Constraints.Builder()
                    .setRequiredNetworkType(NetworkType.CONNECTED)
                    .build())
            .build()

    // Start or refresh the chain now (app open, relay save, self-reschedule).
    // delayMinutes = 0 runs an immediate poll (used to seed on relay save).
    fun schedule(context: Context, delayMinutes: Long = 1) {
      WorkManager.getInstance(context)
          .enqueueUniqueWork(NAME, ExistingWorkPolicy.REPLACE, request(delayMinutes))
    }

    // Arm only if the chain isn't already scheduled — a keepalive that revives a
    // dead chain without disturbing a live one.
    fun ensureScheduled(context: Context) {
      WorkManager.getInstance(context)
          .enqueueUniqueWork(NAME, ExistingWorkPolicy.KEEP, request(1))
    }
  }
}
