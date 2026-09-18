# Validation

Verified on an M4 MacBook Air, 2026-09-19:

- Twenty integration tests, including AddressSanitizer and UndefinedBehaviorSanitizer.
- Clang static analysis.
- Real launchd socket activation, repeated sessions, and idle exit.
- Concurrent clients and final-client SIGKILL cleanup.
- Helper SIGKILL and SIGSTOP recovery.
- Recovery-job restart after SIGKILL.
- Both jobs stopped with stable launch counts during an idle observation.

Lid-close locking retains the existing implementation.
