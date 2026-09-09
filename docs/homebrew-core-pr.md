# Prepared homebrew/core submission

Submit after the public stable source release exists and the owner's trial has
passed. This document is a draft body, not evidence of a submitted pull request.

**Title:** doubleshot 1.0 (new formula)

Doubleshot is a native macOS command-line utility that extends caffeinate-style
sessions to closed-lid operation. A terminal ending or a client crashing releases
its hold automatically, and concurrent sessions are independent.

Upstream: https://github.com/jonathantybirk/doubleshot

The formula builds from the checksummed v1.0 source archive, installs `dshot` and
its man page. It is MIT licensed and
requires macOS 14 or later. Its primary output is a CLI, with no app bundle.

The first invocation installs a protected helper and launchd jobs with administrator
authentication. Lid-close locking uses a private macOS API. Ordinary power
assertions use Apple's caffeinate executable.

Validation to attach at submission:

- Source build and integration tests on the supported macOS CI matrix.
- `brew install --build-from-source`, `brew test`, and `brew audit --strict --online`.
- Local M4 Air trial and recorded runtime recovery results.

Use the release formula emitted by `scripts/package.py`, not the local trial
formula whose URL points to a local file.
