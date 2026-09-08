# Validation

Observed checks on an M4 MacBook Air.

## Automated

- Native release build with Apple Clang, `-Wall -Wextra -Werror`.
- Clang static analysis.
- Integration suite with real processes, Unix sockets and a pseudo-terminal,
  using the separate compile-time fake power backend.

## Observed local results — 2026-09-08

Hardware: Mac16,12, Apple M4. OS: macOS 26.6.2, build 25G83.

- Native release build passed, with warnings treated as errors.
- Clang static analysis completed without diagnostics.
- Initial 15 integration tests passed; expanded 18-test suite also passed with
  AddressSanitizer and UndefinedBehaviorSanitizer enabled (52 seconds).
- Homebrew built and installed the local source formula.
- `brew test`, `brew audit --strict`, and `brew style` passed. Online/public-source
  audit remains for publication because the release is not public yet.
- Installed production service: live timed hold/restoration passed.
- Live command output and exit status preservation passed.
- Live overlapping sessions passed; killing the final client restored sleep.
- Killing the live root service restored sleep and launchd restarted it.
- Freezing the live root service triggered the independent reaper; normal sleep
  returned in 21 seconds and launchd restarted the service.
- Uninstalling while a live hold existed restored normal sleep; reinstalling
  brought back both service jobs with zero active sessions.

The installed CLI and protected helper have matching SHA-256 digests. The native
binary is approximately 91 KiB. Normal sleep was enabled at handoff.

Physical lid-close and display-lock verification is pending the owner’s trial.

## Physical trial still required

- Close/open the M4 Air on battery with a running command; confirm progress
  continues and the screen is dark and locked on return.
- Repeat with concurrent terminals; closing one must leave the other protected.
- Plug/unplug AC while the lid is closed and check for interrupted execution.
- End the final session and confirm ordinary lid-close sleep returns.
- Use normally for a day, then decide whether to publish.

## Simplified setup and messages

The updated CLI builds and passes all 18 integration tests and Clang static analysis.
The installed Homebrew CLI contains the new messages and first-run setup. The
existing helper continues serving the owner's active session. A fresh-install
authentication test is pending an idle session.
