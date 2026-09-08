# Doubleshot

**Caffeinate for closed lids.**

Run `dshot` to keep your Mac running with the lid closed. Normal sleep returns when
the command or terminal session ends, including if the client crashes.

```sh
dshot                         # until Ctrl+C or terminal closure
dshot -t 3600                 # one hour
dshot -- make                 # until make finishes
dshot -w 12345                # until this process exits
dshot -di -- ./backup.sh      # familiar caffeinate flags
```

Native C, no third-party runtime dependencies, no menu-bar app. macOS 14 or later.
No agent detection, network access, telemetry, temperature policy, or battery policy.

## Install

For the local trial, build from source:

```sh
make
./build/dshot install
./build/dshot doctor
./build/dshot
```

`install` asks for administrator authentication once. It installs a protected copy
of the helper and two launchd jobs. Normal `dshot` invocations run as your user and
do not invoke sudo. Build requires Apple's Command Line Tools (`xcode-select --install`).

The Homebrew formula and release tooling are included. Once the public release and
tap have been published, installation will be:

```sh
brew install jonathantybirk/tap/doubleshot
dshot install
```

An official `homebrew/core` submission is planned; availability there is not assumed.

## Caffeinate compatibility

`-d`, `-i`, `-m`, `-s`, `-u`, `-t seconds`, and `-w pid` use the familiar interface.
Apple's `/usr/bin/caffeinate` provides ordinary assertions. Doubleshot adds the
system-wide closed-lid hold on both AC and battery.

- No assertion flags means `-i`.
- A command takes precedence over `-t` and `-w`.
- Without a command, a timeout and watched PID both apply: the first to end wins.
- `-t 0` means no deadline. Values must be nonnegative integers.
- Program exit status and stdout/stderr are preserved. Use `--` before a command
  whose name is a Doubleshot administrative subcommand.
- Ctrl+C ends the hold. Terminal closure also ends it even if work outlives the
  terminal. Noninteractive runs follow the invocation/workload lifetime.
- In tmux, the pane owns the terminal. Detaching leaves the pane alive; closing
  the pane ends the hold.
- Concurrent invocations compose. Ending one does not cancel the others.

When the lid closes, Doubleshot removes its display-awake assertions, locks the
session, and requests display sleep. This takes precedence over `-d`/`-u` while
closed. Display sleep may also affect connected external displays. Assertions are
re-created when the lid opens; a user-active (`-u`) assertion can therefore be
issued again on reopening. This is an intentional difference from a stock invocation.

Screen locking uses Apple's private login-framework API. If locking cannot be
verified, the client releases its hold. `doctor` checks API availability; an actual
lid-close test is required to verify behavior on a particular Mac and OS version.

## Two settings

```sh
dshot config init       # create without overwriting an existing file
dshot config            # print the path and effective settings
```

The file is `~/.config/doubleshot/config.toml`, or
`$XDG_CONFIG_HOME/doubleshot/config.toml` when set:

```toml
alias_caffeinate = false
lock_on_close = true
```

To let typed `caffeinate` commands follow the first setting, add this one line to
your `.zshrc` (or use `bash` in `.bashrc`):

```sh
eval "$(dshot shell-init zsh)"
```

For fish, add `dshot shell-init fish | source` to its configuration.

Now `alias_caffeinate = true` routes that shell's `caffeinate` through Doubleshot;
`false` routes it directly to `/usr/bin/caffeinate`. Changes apply on the next
invocation, with no restart. This replaces that shell's existing `caffeinate`
function if it has one. It does not replace Apple's executable, affect other
applications, or scan for caffeinate processes. Fresh scripts must explicitly
source the shell integration or call `dshot` themselves.

`lock_on_close` is read at session start. Setting it to `false` skips the explicit
lock; the display-sleep request remains. No config setting executes shell code.

## Inspect, update, remove

```sh
dshot status
dshot doctor
tail -n 50 /var/log/doubleshot.log
```

After upgrading the CLI, run `dshot install` to update the protected service copy.
An update ends active holds before replacing the service. `status` prints both
CLI and service versions so a mismatch is visible.

```sh
dshot uninstall               # restore owned settings, remove both launchd jobs
brew uninstall doubleshot    # if installed through Homebrew
```

Remove the optional shell-init line and function if you enabled them. User config
is deliberately retained. Uninstall the service **before** removing the CLI.
Deleting only the Homebrew package is not a privileged service uninstallation.

Doubleshot refuses to acquire when `SleepDisabled` is already enabled by another
tool. It restores only holds it recorded. If a manual change or another utility
enabled the flag, resolve that owner first; `sudo pmset -a disablesleep 0` is the
explicit manual reset. Multiple unrelated tools writing this global setting do
not have a shared ownership protocol.

## Recovery contract

A local socket ties each lease to its authenticated client. Disconnect drops it;
otherwise five-second heartbeats keep a twenty-second lease alive. Client process
identity includes its start time, not just its PID.

The root service journals intent before enabling sleep suppression, verifies the
result, and retains the journal until restoration succeeds. Power commands are
bounded. launchd restarts a crashed service. A separate short-lived reaper checks
every ten seconds and fences a writer stalled for fifteen seconds before restoring
the setting. The writer lock is inherited by power-command children, so an
orphaned `pmset` cannot race a reset from the reaper.

These are nominal recovery intervals, not hard real-time guarantees. Scheduling,
power-management failures, disabled launchd jobs, or an unresponsive OS can delay
or prevent recovery. A lease is never restored automatically after restart. Losing
the service ends current holds; it does not silently claim the command is protected.

## Development

```sh
make test
```

Integration tests use real sockets, process crashes/freezes, and a pseudo-terminal,
but a compile-time fake power backend. The test executable refuses root and cannot
install a service. The production executable has no environment-selectable power
command, socket location, or test mode.

See [architecture](docs/architecture.md), [validation](docs/validation.md), and
[publication](docs/publication.md). MIT licensed.
