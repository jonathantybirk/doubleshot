# Architecture

`dshot` connects to a launchd-owned Unix socket, starting the root helper on demand.
Each authenticated client holds a lease, renewed every five seconds and expiring
after twenty. The helper restores normal sleep after the last lease and exits
after two seconds without connections.

Before enabling `pmset disablesleep`, the helper writes a durable reset record
under `/var/db/doubleshot/recovery/`. That directory keeps a separate launchd
recovery job alive. The helper explicitly starts recovery and waits for its
acknowledgement before enabling the override.

Recovery checks the writer heartbeat once a second while a reset is pending.
A stalled writer is identified by PID and process start time; recovery kills its
process group and takes the exclusive writer lock before restoring sleep.
Power-command children inherit the lock. The reset record is removed only after
verified restoration; recovery then exits. Pending records also activate recovery
when launchd loads the job after a reboot.

The client handles terminal lifetime, caffeinate assertions, and lid events.
Lid closure locks the session through Apple's private login-framework API and
requests display sleep. Installation registers the socket and recovery jobs.
