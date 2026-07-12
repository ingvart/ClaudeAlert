package no.automasjon.claudeusage

import android.content.Context
import androidx.work.CoroutineWorker
import androidx.work.WorkerParameters
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

// Periodic background poll: get a valid access token (refreshing it if needed),
// fetch usage directly from Anthropic, diff against the stored snapshot, notify
// on alerts, persist, and refresh the widget. No relay or desktop involved.
class PollWorker(appContext: Context, params: WorkerParameters) :
    CoroutineWorker(appContext, params) {

  override suspend fun doWork(): Result = withContext(Dispatchers.IO) {
    val context = applicationContext
    if (!Prefs.isLoggedIn(context)) return@withContext Result.success()  // Not set up yet.

    val current = try {
      val token = TokenManager.validAccessToken(context)
      UsageApi.fetch(token).toWindows()
    } catch (e: TokenManager.NotLoggedInException) {
      return@withContext Result.success()
    } catch (e: UsageHttpException) {
      // 401/403 means the login is no longer valid; stop hammering and wait for
      // the user to log in again. Other statuses (429, 5xx) are transient.
      return@withContext if (e.status == 401 || e.status == 403) Result.success()
          else Result.retry()
    } catch (e: Exception) {
      return@withContext Result.retry()  // Network blip; try again later.
    }

    val previous = Prefs.lastSnapshot(context)
    val alerts = computeAlerts(previous, current, Prefs.config(context))

    var notificationId = 1000
    for (alert in alerts) {
      Notifications.post(context, notificationId++, "Claude usage", alert.message)
    }

    Prefs.setLastSnapshot(context, current)
    UsageWidget.updateAll(context)
    Result.success()
  }
}
