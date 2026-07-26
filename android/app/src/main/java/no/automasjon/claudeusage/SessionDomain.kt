package no.automasjon.claudeusage

import kotlinx.serialization.Serializable

// Wire model for GET <relay>/sessions (see relay/relay.py and PLAN.md §6.B).
// Mirrors the desktop's C++ session model one-for-one so both clients behave
// identically. Numbers are read as Double because the relay emits `now` as a
// float and the rest as ints; we truncate to whole seconds in the domain view.
// Unknown fields are ignored by the parser.
@Serializable
data class SessionEntryDto(
    val session_id: String = "",
    val cwd: String = "",
    val title: String = "",
    val state: String = "unknown",
    val last_seen: Double = 0.0,
    val last_landed_at: Double? = null,
    val last_duration: Double? = null,
)

@Serializable
data class LandingDto(
    val session_id: String = "",
    val title: String = "",
    val cwd: String = "",
    val landed_at: Double = 0.0,
    val duration: Double = 0.0,
)

@Serializable
data class SessionsDto(
    val sessions: List<SessionEntryDto> = emptyList(),
    val landings: List<LandingDto> = emptyList(),
    val now: Double = 0.0,
    val rev: Long = 0,  // Bumps only on a new landing; used for conditional polls.
)

// Domain views (whole-second timestamps on the relay clock).
data class SessionEntry(
    val sessionId: String,
    val cwd: String,
    val title: String,
    val state: String,
    val lastSeen: Long,
    val lastLandedAt: Long?,
    val lastDuration: Long?,
)

// A turn that ran long enough (and left no background work) to be worth a
// notification — "your session finished the substantial thing and is waiting".
data class Landing(
    val sessionId: String,
    val title: String,
    val cwd: String,
    val landedAt: Long,
    val duration: Long,
)

data class SessionInventory(
    val sessions: List<SessionEntry>,
    val landings: List<Landing>,
    val now: Long,  // Relay clock at snapshot time (skew-free "landed N ago").
    val rev: Long,  // Revision to send back as ?since= on the next poll.
)

fun SessionsDto.toInventory(): SessionInventory = SessionInventory(
    sessions = sessions.map {
      SessionEntry(
          sessionId = it.session_id,
          cwd = it.cwd,
          title = it.title,
          state = it.state,
          lastSeen = it.last_seen.toLong(),
          lastLandedAt = it.last_landed_at?.toLong(),
          lastDuration = it.last_duration?.toLong(),
      )
    },
    landings = landings.map {
      Landing(
          sessionId = it.session_id,
          title = it.title,
          cwd = it.cwd,
          landedAt = it.landed_at.toLong(),
          duration = it.duration.toLong(),
      )
    },
    now = now.toLong(),
    rev = rev,
)

// Stable identity of a landing for dedupe (PLAN.md §6.B): the same
// (session_id, landed_at) never notifies twice. landed_at is monotonic per
// session, so this also distinguishes successive landings of one session.
fun landingKey(landing: Landing): String = "${landing.sessionId}:${landing.landedAt}"

// Returns the landings in `current` not already in `seen`, recording them in
// `seen`. Faithful port of the C++ select_new_landings. The caller seeds `seen`
// from the first poll so pre-existing landings never notify.
fun selectNewLandings(current: List<Landing>, seen: MutableSet<String>): List<Landing> {
  val fresh = ArrayList<Landing>()
  for (landing in current) {
    if (seen.add(landingKey(landing))) fresh.add(landing)
  }
  return fresh
}

// Accepts the relay base URL or the legacy ".../usage" publish URL (matches the
// C++ relay_base): strips a trailing "/usage" and slashes.
fun relayBase(url: String): String {
  var u = url.trim()
  if (u.endsWith("/usage")) u = u.substring(0, u.length - "/usage".length)
  return u.trimEnd('/')
}

// A short, human "4m 44s" / "2h 3m" for durations and "landed N ago".
fun formatDuration(seconds: Long): String {
  if (seconds < 60) return "${seconds}s"
  val minutes = seconds / 60
  val secs = seconds % 60
  if (minutes < 60) return if (secs == 0L) "${minutes}m" else "${minutes}m ${secs}s"
  val hours = minutes / 60
  val mins = minutes % 60
  return if (mins == 0L) "${hours}h" else "${hours}h ${mins}m"
}

// Best-effort display name for a session: its title, else the last path segment
// of its working directory, else a generic label.
fun sessionLabel(title: String, cwd: String): String {
  if (title.isNotBlank()) return title
  val leaf = cwd.trimEnd('/', '\\').substringAfterLast('/').substringAfterLast('\\')
  return leaf.ifBlank { "Claude session" }
}
