package no.automasjon.claudeusage

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.background
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp

// Live view of the relay's session inventory. Sessions currently working sort
// first, then most-recently-seen. Timestamps are shown relative to the relay's
// own clock (inventory.now) so there's no client/relay skew.
@Composable
fun SessionsPanel(inventory: SessionInventory?, note: String) {
  Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
    Text("Remote sessions", style = MaterialTheme.typography.titleMedium)

    val sessions = inventory?.sessions
    if (sessions.isNullOrEmpty()) {
      Text(
          note.ifBlank { "No sessions reported yet." },
          style = MaterialTheme.typography.bodySmall,
          color = Color(0xFF888888))
      return@Column
    }

    val now = inventory.now
    val ordered = sessions.sortedWith(
        compareByDescending<SessionEntry> { it.state == "working" }
            .thenByDescending { it.lastSeen })

    for (session in ordered) {
      Row(
          modifier = Modifier.fillMaxWidth(),
          verticalAlignment = Alignment.CenterVertically,
          horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        Box(
            modifier = Modifier.size(10.dp).clip(CircleShape)
                .background(stateColor(session.state)))
        Column(modifier = Modifier.fillMaxWidth()) {
          Text(
              sessionLabel(session.title, session.cwd),
              style = MaterialTheme.typography.bodyMedium,
              maxLines = 1, overflow = TextOverflow.Ellipsis)
          Text(
              session.cwd,
              style = MaterialTheme.typography.bodySmall,
              color = Color(0xFF888888),
              maxLines = 1, overflow = TextOverflow.Ellipsis)
          Text(
              stateLine(session, now),
              style = MaterialTheme.typography.bodySmall,
              color = Color(0xFF888888))
        }
      }
    }

    if (note.isNotBlank()) {
      Text(note, style = MaterialTheme.typography.bodySmall, color = Color(0xFF888888),
          modifier = Modifier.padding(top = 2.dp))
    }
  }
}

private fun stateColor(state: String): Color = when (state) {
  "working" -> Color(0xFFF9A825)  // amber — busy
  "idle" -> Color(0xFF2E7D32)     // green — done, waiting
  "ended" -> Color(0xFF9E9E9E)    // grey — gone
  else -> Color(0xFFBDBDBD)
}

private fun stateLine(session: SessionEntry, now: Long): String {
  val landed = session.lastLandedAt?.let { "landed ${formatDuration((now - it).coerceAtLeast(0))} ago" }
  return when (session.state) {
    "working" -> "working…"
    "idle" -> landed ?: "idle"
    "ended" -> "ended"
    else -> session.state
  }
}
