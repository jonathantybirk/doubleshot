# Release checklist

Publish when Jonathan explicitly gives the word.

1. Review the trial results and run `make test`.
2. Make `jonathantybirk/doubleshot` public and tag `v0.1.0`.
3. Run `python3 scripts/package.py` and attach the source archive to the GitHub release.
4. Copy `dist/doubleshot.rb` into `jonathantybirk/homebrew-tap/Formula/`.
5. Build and test the formula, run `brew audit --strict --online`, and publish the tap.
6. Submit the formula to `Homebrew/homebrew-core` for `brew install doubleshot`.

[Homebrew contribution guide](https://docs.brew.sh/Adding-Software-to-Homebrew).
