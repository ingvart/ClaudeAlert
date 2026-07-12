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
import androidx.compose.material3.Divider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
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

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    Notifications.ensureChannel(this)
    requestNotificationPermissionIfNeeded()
    schedulePeriodicPoll(this)
    setContent {
      SettingsScreen(
          onPickSound = { pickSound() },
          onOpenUrl = { url -> startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) })
    }
  }

  private fun pickSound() {
    val intent = Intent(RingtoneManager.ACTION_RINGTONE_PICKER).apply {
      putExtra(RingtoneManager.EXTRA_RINGTONE_TYPE, RingtoneManager.TYPE_NOTIFICATION)
      putExtra(RingtoneManager.EXTRA_RINGTONE_TITLE, "Alert sound")
      putExtra(RingtoneManager.EXTRA_RINGTONE_SHOW_DEFAULT, true)
      putExtra(RingtoneManager.EXTRA_RINGTONE_EXISTING_URI, Prefs.soundUri(this@MainActivity))
    }
    ringtoneLauncher.launch(intent)
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
fun SettingsScreen(onPickSound: () -> Unit, onOpenUrl: (String) -> Unit) {
  val context = LocalContext.current
  val config = remember { Prefs.config(context) }
  var loggedIn by remember { mutableStateOf(Prefs.isLoggedIn(context)) }
  var code by remember { mutableStateOf("") }
  var weekly by remember { mutableStateOf(config.weeklyThreshold.toString()) }
  var fiveHourFloor by remember { mutableStateOf(config.fiveHourDropFloor.toString()) }
  var notifyOnDrop by remember { mutableStateOf(config.notifyOnDrop) }
  var status by remember { mutableStateOf<String?>(null) }
  var statusOk by remember { mutableStateOf(false) }
  val scope = rememberCoroutineScope()

  MaterialTheme {
    Column(
        modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)) {
      Text("Claude Usage Monitor", style = MaterialTheme.typography.titleLarge)

      if (loggedIn) {
        Text("Signed in ✓  The app fetches your usage directly and renews its " +
            "own session automatically — you won't need to log in again.")
        OutlinedButton(onClick = {
          Prefs.logout(context)
          loggedIn = false
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
                val result = withContext(Dispatchers.IO) {
                  runCatching {
                    val tokens = OAuth.exchange(
                        code.trim(), Prefs.pendingVerifier(context), Prefs.pendingState(context))
                    Prefs.saveTokens(context, tokens)
                    val windows = UsageApi.fetch(tokens.accessToken).toWindows()
                    Prefs.setLastSnapshot(context, windows)
                    UsageWidget.updateAll(context)
                    windows
                  }
                }
                result.fold(
                    onSuccess = { windows ->
                      loggedIn = true
                      statusOk = true
                      code = ""
                      val five = windows.firstOrNull { it.name == "5-hour" }?.percent
                      val wk = windows.firstOrNull { it.name == "weekly" }?.percent
                      status = "Signed in ✓   5h ${five ?: "-"}%   ·   weekly ${wk ?: "-"}%"
                    },
                    onFailure = { error ->
                      statusOk = false
                      status = "Login failed: ${error.message ?: "unknown"}"
                    })
              }
            }) { Text("2. Complete login") }
      }

      Divider()

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
      OutlinedButton(onClick = onPickSound) { Text("Pick alert sound") }
      OutlinedButton(onClick = {
        Notifications.post(context, 9999, "Claude usage (test)",
            "Test alert — this is how notifications will sound.")
      }) { Text("Send test notification") }
      Button(onClick = {
        Prefs.setConfig(context, config.copy(
            notifyOnDrop = notifyOnDrop,
            weeklyThreshold = weekly.toIntOrNull() ?: config.weeklyThreshold,
            fiveHourDropFloor = fiveHourFloor.toIntOrNull() ?: config.fiveHourDropFloor))
        statusOk = true
        status = "Settings saved."
      }) {
        Text("Save settings")
      }

      // Manual refresh for when you just want to check right now.
      if (loggedIn) {
        TextButton(onClick = {
          status = "Refreshing…"
          scope.launch {
            val result = withContext(Dispatchers.IO) {
              runCatching {
                val token = TokenManager.validAccessToken(context)
                val windows = UsageApi.fetch(token).toWindows()
                Prefs.setLastSnapshot(context, windows)
                UsageWidget.updateAll(context)
                windows
              }
            }
            result.fold(
                onSuccess = { windows ->
                  statusOk = true
                  val five = windows.firstOrNull { it.name == "5-hour" }?.percent
                  val wk = windows.firstOrNull { it.name == "weekly" }?.percent
                  status = "5h ${five ?: "-"}%   ·   weekly ${wk ?: "-"}%"
                },
                onFailure = { error ->
                  statusOk = false
                  status = "Refresh failed: ${error.message ?: "unknown"}"
                })
          }
        }) { Text("Check now") }
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
