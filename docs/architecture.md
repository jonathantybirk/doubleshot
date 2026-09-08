# Architecture

One native executable has three runtime roles:

1. **Client:** parse caffeinate-compatible arguments, hold a socket lease, monitor
   terminal/workload lifetime, run Apple's caffeinate, and handle lid events in the
   logged-in session.
2. **Root service:** accept one registered local user's clients, own the lease set,
   journal and apply the fixed `pmset -a disablesleep 0|1` commands, verify state,
   and release when the set becomes empty.
3. **Reaper:** a launchd StartInterval job. If a journal is pending, obtain the
   exclusive writer lock. If the writer is stalled, verify its recorded identity,
   fence its process group, then obtain the lock and reset before removing the journal.

## Files and authority

| Path | Purpose |
|---|---|
| `/Library/PrivilegedHelperTools/io.doubleshot/dshot` | Root-owned executable, protected parents |
| `/Library/LaunchDaemons/io.doubleshot.daemon.plist` | KeepAlive root service |
| `/Library/LaunchDaemons/io.doubleshot.reaper.plist` | Recovery every ten seconds |
| `/var/db/doubleshot/owner` | Registered UID |
| `/var/db/doubleshot/restore` | Durable pending-reset record, writer PID/start time/continuous timestamp |
| `/var/db/doubleshot/writer.lock` | Exclusive mutation ownership, inherited by pmset children |
| `/var/run/doubleshot/control.sock` | Socket mode 0600, registered user owner |
| `/var/run/doubleshot/heartbeat` | Root-written continuous-clock progress, refreshed while active |
| `/var/log/doubleshot.log` | State transitions and failures |

The socket uses kernel peer credentials and peer PID. Clients verify that the
server is root. Requests contain no executable, path, shell fragment, or user
configuration: `A` acquire, `H` renew, `R` release, `S` status. Messages, replies,
connection lifetimes, and the number of connections are bounded. A connection
holds at most one lease; repeated acquire/release is idempotent.

The global setting has no cross-application ownership protocol. Acquisition
requires an observed zero baseline. A recovery marker is durable before enable.
The marker remains on failed reset and disappears only after verified zero.
Reboot/restart never revives old leases. A foreign preexisting hold is rejected.

The daemon is a process-group leader. Its bounded power children inherit the writer
lock, so they continue excluding a new writer even if their parent dies. The
reaper kills the old group before takeover. It rejects a live, mismatched PID start
time. macOS retains process-group identity while members remain; after no members
remain the exclusive lock is available without killing anything.

## Event and lifecycle behavior

IOKit notifications wake an otherwise blocked event thread for lid and power-source
changes. The client also rechecks the lid every five seconds. The main loops poll
at most once per second; client heartbeat is five seconds, expiry twenty seconds.
There is no system-wide process scan. The root service verifies live power state
every ten seconds while a hold exists and re-applies on power-source/wake events.

Client signal handlers only record intent. Cleanup runs in normal process context.
The controlling tty is monitored for hangup; foreground input is never read by
Doubleshot. Commands inherit the terminal/process group and run unprivileged.
The wrapper forwards termination signals to a surviving child, but does not
force-kill a command simply because the sleep service failed.

The client removes its display/user-active assertions on lid closure. It invokes
`SACLockScreenImmediate` in the user's session and checks
`CGSSessionScreenIsLocked` before requesting `pmset displaysleepnow`. This API and
the lock-state key are private and must be physically verified after OS changes.

## Boundaries

- Corrupted root metadata with an active writer is not treated as permission to
  kill an arbitrary PID. Recovery reports failure instead.
- Persistent OS failures, disabled jobs, and simultaneous failures of the OS and
  both recovery paths cannot be given a userspace deadline.
- Restoring normal sleep permits macOS to sleep; it is not a forced sleep command.
- First-run setup installs the root service with administrator authentication. Upgrades end active leases before replacement.
- The test backend is compiled into a separate executable which refuses root.
