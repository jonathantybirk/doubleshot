# Prepared homebrew/core submission

Submit after the public stable source release exists and the owner's trial has
passed. This document is a draft body, not evidence of a submitted pull request.

**Title:** doubleshot 0.1.0 (new formula)

Doubleshot is a native macOS command-line utility that extends caffeinate-style
sessions to closed-lid operation. A terminal ending or a client crashing releases
its hold automatically, and concurrent sessions are independent.

Upstream: https://github.com/jonathantybirk/doubleshot

The formula builds from the checksummed v0.1.0 source archive, installs `dshot` and
its man page, and has no third-party runtime dependencies. It is MIT licensed and
requires macOS 14 or later. Its primary output is a CLI, with no app bundle.

Closed-lid operation requires a one-time explicit `dshot install`, which installs
a protected root helper and launchd jobs. The formula itself never invokes sudo or
changes system power settings. Caveats explain helper setup/update and removal.
The optional lid-close lock uses a private macOS API; runtime checks detect missing
support. Ordinary power assertions use Apple's installed caffeinate executable.

Validation to attach at submission:

- Source build and integration tests on the supported macOS CI matrix.
- `brew install --build-from-source`, `brew test`, and `brew audit --strict --online`.
- Local M4 Air trial and recorded runtime recovery results.

Use the release formula emitted by `scripts/package.py`, not the local trial
formula whose URL points to a local file.
