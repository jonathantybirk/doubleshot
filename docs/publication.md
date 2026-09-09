# Release checklist

1. Build and test the source.
2. Commit and tag the version.
3. Run `python3 scripts/package.py` and attach the source archive to its GitHub release.
4. Copy `dist/doubleshot.rb` into the tap's `Formula/` directory, test, and push.

Jonathan writes and submits Homebrew core pull requests manually.
