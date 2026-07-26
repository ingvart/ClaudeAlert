package no.automasjon.claudeusage

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.media.RingtoneManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.work.Constraints
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.NetworkType
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.concurrent.TimeUnit

class MainActivity : ComponentActivity() {
  private val permissionLauncher =
      registerForActivityResult(ActivityResultContracts.RequestPermission()) {}

  private val ringtoneLauncher =
      registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode == RESULT_OK) {
          @Suppress("DEPRECATION")
          val uri = result.data?.getParcelableExtra<Uri>(
              RingtoneManager.EXTRA_RINGTONE_PICKED_URI)
          Prefs.setSoundUri(this, uri)
          Notifications.applySound(this)
        }
      }

  private val sessionRingtoneLauncher =
      registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode == RESULT_OK) {
          @Suppress("DEPRECATION")
          val uri = result.data?.getParcelableExtra<Uri>(
              RingtoneManager.EXTRA_RINGTONE_PICKED_URI)
          Prefs.setSessionSoundUri(this, uri)
          Notifications.ensureLandingChannel(this)
        }
      }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    Notifications.ensureChannel(this)
    Notifications.ensureLandingChannel(this)
    requestNotificationPermissionIfNeeded()
    schedulePeriodicPoll(this)
    SessionPollWorker.ensureScheduled(this)  // ~1-min session-landing chain
    setContent {
      SettingsScreen(
          onPickSound = { pickSound() },
          onPickSessionSound = { pickSessionSound() },
          onOpenUrl = { url -> startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) })
    }
  }

  private fun pickSound() {
    val intent = Intent(RingtoneManager.ACTION_RINGTONE_PICKER).apply {
      putExtra(RingtoneManager.EXTRA_RINGTONE_TYPE, RingtoneManager.TYPE_NOTIFICATION)
      putExtra(RingtoneManager.EXTRA_RINGTONE_TITLE, "Usage-freed sound")
      putExtra(RingtoneManager.EXTRA_RINGTONE_SHOW_DEFAULT, true)
      putExtra(RingtoneManager.EXTRA_RINGTONE_EXISTING_URI, Prefs.soundUri(this@MainActivity))
    }
    ringtoneLauncher.launch(intent)
  }

  private fun pickSessionSound() {
    val intent = Intent(RingtoneManager.ACTION_RINGTONE_PICKER).apply {
      putExtra(RingtoneManager.EXTRA_RINGTONE_TYPE, RingtoneManager.TYPE_NOTIFICATION)
      putExtra(RingtoneManager.EXTRA_RINGTONE_TITLE, "Session-done sound")
      putExtra(RingtoneManager.EXTRA_RINGTONE_SHOW_DEFAULT, true)
      putExtra(RingtoneManager.EXTRA_RINGTONE_EXISTING_URI,
          Prefs.sessionSoundUri(this@MainActivity))
    }
    sessionRingtoneLauncher.launch(intent)
  }

  private fun requestNotificationPermissionIfNeeded() {
    if (Build.VERSION.SDK_INT >= 33 &&
        ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) !=
            PackageManager.PERMISSION_GRANTED) {
      permissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
    }
  }
}

@Composable
fun SettingsScreen(
    onPickSound: () -> Unit,
    onPickSessionSound: () -> Unit,
    onOpenUrl: (String) -> Unit,
) {
  val context = LocalContext.current
  val config = remember { Prefs.config(context) }
  var loggedIn by remember { mutableStateOf(Prefs.isLoggedIn(context)) }
  var code by remember { mutableStateOf("") }
  var weekly by remember { mutableStateOf(config.weeklyThreshold.toString()) }
  var fiveHourFloor by remember { mutableStateOf(config.fiveHourDropFloor.toString()) }
  var notifyOnDrop by remember { mutableStateOf(config.notifyOnDrop) }
  var status by remember { mutableStateOf<String?>(null) }
  var statusOk by remember { mutableStateOf(false) }

  // Usage panel state, seeded from the last saved snapshot then refreshed live.
  var windows by remember { mutableStateOf(Prefs.lastSnapshot(context)) }
  var extra by remember { mutableStateOf<ExtraUsageDto?>(null) }
  var updatedNote by remember {
    mutableStateOf(if (windows.isEmpty()) "" else "Showing last saved data…")
  }
  var nowMs by remember { mutableStateOf(System.currentTimeMillis()) }
  val scope = rememberCoroutineScope()

  // Relay (remote session monitoring) — optional, independent of the login.
  var relayUrl by remember { mutableStateOf(Prefs.relayUrl(context)) }
  var relayToken by remember { mutableStateOf(Prefs.relayToken(context)) }
  var sessions by remember { mutableStateOf<SessionInventory?>(null) }
  var sessionNote by remember { mutableStateOf("") }

  // Keep the reset countdowns ticking while the screen is open.
  LaunchedEffect(Unit) {
    while (true) {
      nowMs = System.currentTimeMillis()
      delay(10_000)
    }
  }

  // Live session list while the screen is open, using the SAVED relay config
  // (not the in-progress text fields). Silent when no relay is configured.
  LaunchedEffect(Unit) {
    while (true) {
      val url = Prefs.relayUrl(context)
      if (url.isNotBlank()) {
        val result = withContext(Dispatchers.IO) {
          runCatching { SessionApi.fetch(url, Prefs.relayToken(context)) }
        }
        result.fold(
            onSuccess = { sessions = it; sessionNote = "" },
            onFailure = { sessionNote = "Can't reach relay — check URL/token." })
      }
      delay(15_000)
    }
  }

  // Fetch fresh usage; update panel, stored snapshot, and widget. Returns success.
  val refresh: suspend () -> Boolean = refresh@{
    val result = withContext(Dispatchers.IO) {
      runCatching {
        val token = TokenManager.validAccessToken(context)
        val dto = UsageApi.fetch(token)
        val fresh = dto.toWindows()
        Prefs.setLastSnapshot(context, fresh)
        UsageWidget.updateAll(context)
        fresh to dto.extra_usage
      }
    }
    result.fold(
        onSuccess = { (fresh, eu) ->
          windows = fresh
          extra = eu
          updatedNote = "Updated just now · auto-refreshes every 15 min"
          true
        },
        onFailure = { false })
  }

  // Refresh once when the screen opens / after a fresh login.
  LaunchedEffect(loggedIn) { if (loggedIn) refresh() }

  MaterialTheme {
    Column(
        modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)) {
      Text("Claude Usage Monitor", style = MaterialTheme.typography.titleLarge)

      // ---- Informative usage panel (top) ----
      UsagePanel(windows, extra, updatedNote, nowMs)
      if (loggedIn) {
        TextButton(onClick = {
          status = "Refreshing…"
          scope.launch {
            val ok = refresh()
            statusOk = ok
            status = if (ok) "Updated ✓" else "Refresh failed — check connection."
          }
        }) { Text("Check now") }
      }

      // ---- Remote sessions (only once a relay is configured) ----
      if (relayUrl.isNotBlank()) {
        HorizontalDivider()
        SessionsPanel(sessions, sessionNote)
      }

      HorizontalDivider()

      // ---- Settings (below) ----
      if (loggedIn) {
        Text("Signed in ✓  Fetches usage directly and renews its own session " +
            "automatically — no repeat logins.")
        OutlinedButton(onClick = {
          Prefs.logout(context)
          loggedIn = false
          windows = emptyList()
          extra = null
          updatedNote = ""
          status = null
        }) { Text("Log out") }
      } else {
        Text("Sign in once with your Claude account. The app then fetches usage " +
            "directly and refreshes its own session — no repeated logins, no " +
            "desktop or relay needed.")
        Button(onClick = {
          val begin = OAuth.begin()
          Prefs.setPendingPkce(context, begin.verifier, begin.state)
          onOpenUrl(begin.url)
        }) { Text("1. Open Claude login") }
        Text("Approve in the browser, copy the code it shows, paste it below:")
        OutlinedTextField(
            value = code,
            onValueChange = { code = it },
            label = { Text("Authorization code") },
            modifier = Modifier.fillMaxWidth())
        Button(
            enabled = code.isNotBlank(),
            onClick = {
              status = "Signing in…"
              scope.launch {
                val exchanged = withContext(Dispatchers.IO) {
                  runCatching {
                    val tokens = OAuth.exchange(
                        code.trim(), Prefs.pendingVerifier(context), Prefs.pendingState(context))
                    Prefs.saveTokens(context, tokens)
                  }
                }
                exchanged.fold(
                    onSuccess = {
                      code = ""
                      loggedIn = true  // triggers the refresh effect → panel fills in
                      statusOk = true
                      status = "Signed in ✓"
                    },
                    onFailure = { error ->
                      statusOk = false
                      status = "Login failed: ${error.message ?: "unknown"}"
                    })
              }
            }) { Text("2. Complete login") }
      }

      OutlinedTextField(
          value = weekly,
          onValueChange = { weekly = it },
          label = { Text("Weekly alert % (rising)") })
      OutlinedTextField(
          value = fiveHourFloor,
          onValueChange = { fiveHourFloor = it },
          label = { Text("5-hour drop alarm above %") })
      Row(verticalAlignment = Alignment.CenterVertically) {
        Switch(checked = notifyOnDrop, onCheckedChange = { notifyOnDrop = it })
        Text(
            "Notify when usage drops or resets (capacity freed)",
            modifier = Modifier.padding(start = 8.dp))
      }
      HorizontalDivider()
      Text("Remote session monitoring (optional)",
          style = MaterialTheme.typography.titleMedium)
      Text("Point this at your self-hosted relay to get notified when a Claude " +
          "Code session on your computer finishes working. Leave blank to disable.",
          style = MaterialTheme.typography.bodySmall)
      OutlinedTextField(
          value = relayUrl,
          onValueChange = { relayUrl = it },
          label = { Text("Relay URL (e.g. http://192.168.0.10:8787)") },
          modifier = Modifier.fillMaxWidth())
      OutlinedTextField(
          value = relayToken,
          onValueChange = { relayToken = it },
          label = { Text("Relay token") },
          modifier = Modifier.fillMaxWidth())

      Text("Sounds", style = MaterialTheme.typography.titleMedium)
      Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        OutlinedButton(onClick = onPickSound, modifier = Modifier.weight(1f)) {
          Text("Usage-freed sound")
        }
        OutlinedButton(onClick = {
          Notifications.post(context, 9999, "Claude usage (test)",
              "Usage freed — this is the usage sound.")
        }, modifier = Modifier.weight(1f)) { Text("Test") }
      }
      Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        OutlinedButton(onClick = onPickSessionSound, modifier = Modifier.weight(1f)) {
          Text("Session-done sound")
        }
        OutlinedButton(onClick = {
          Notifications.postLanding(context, 9998, "Test session", 284)
        }, modifier = Modifier.weight(1f)) { Text("Test") }
      }
      Button(onClick = {
        Prefs.setConfig(context, config.copy(
            notifyOnDrop = notifyOnDrop,
            weeklyThreshold = weekly.toIntOrNull() ?: config.weeklyThreshold,
            fiveHourDropFloor = fiveHourFloor.toIntOrNull() ?: config.fiveHourDropFloor))
        Prefs.setRelay(context, relayUrl, relayToken)
        // Start the ~1-min session chain immediately (delay 0): seeds the
        // seen-landings set now and begins fast polling, so the next NEW landing
        // notifies reliably.
        if (relayUrl.isNotBlank()) {
          SessionPollWorker.schedule(context, 0)
        }
        statusOk = true
        status = "Settings saved."
      }) {
        Text("Save settings")
      }

      status?.let { message ->
        Text(message, color = if (statusOk) Color(0xFF2E7D32) else Color(0xFFC62828))
      }
    }
  }
}

private fun schedulePeriodicPoll(context: Context) {
  val request = PeriodicWorkRequestBuilder<PollWorker>(Prefs.pollMinutes(context), TimeUnit.MINUTES)
      .setConstraints(Constraints.Builder().setRequiredNetworkType(NetworkType.CONNECTED).build())
      .build()
  WorkManager.getInstance(context)
      .enqueueUniquePeriodicWork("usage_poll", ExistingPeriodicWorkPolicy.UPDATE, request)
}
