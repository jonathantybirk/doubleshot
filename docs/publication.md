# Publication plan

Target repositories: `jonathantybirk/doubleshot` and `jonathantybirk/homebrew-tap`.
They are targets, not a claim that either has been published.

The current build is for the owner's day-long trial. Public release follows the
trial outcome. Do not manufacture a successful trial or publish automatically
without its result.

## First public release

1. Record actual validation results in `docs/validation.md`; resolve release-blocking failures.
2. Commit the reviewed source, update the changelog, and tag `v0.1.0`.
3. Create the public MIT-licensed GitHub repository and push source/tag.
4. Run `python3 scripts/package.py`. Upload the generated, checksummed source
   archive to the matching GitHub release; do not replace an existing release asset.
5. Put the generated `dist/doubleshot.rb` in `Formula/` in the tap repository.
6. Build/install/test the tap formula, audit it, and publish the tap.
7. Submit a new-formula pull request to `Homebrew/homebrew-core` immediately after
   the public stable release is available. Include the source URL, checksum,
   license, macOS restriction, test evidence and explicit `dshot install` requirement.

The core submission is in addition to the tap, not deferred until the tap gains
users. Maintainer acceptance and timing are outside the project's control.

## Formula checks

```sh
brew install --build-from-source jonathantybirk/tap/doubleshot
brew test jonathantybirk/tap/doubleshot
brew audit --strict --online jonathantybirk/tap/doubleshot
```

Review macOS-only support, use of a private locking API, and the one-time privileged
setup candidly in the PR. The formula builds a native CLI from source and does not
run sudo, install root services, or change power settings during `brew install`.

After the tap is available, users can run
`brew install jonathantybirk/tap/doubleshot`. Once the tap is installed, the short
name also works if there is no conflicting core formula. Global discovery through
plain `brew install doubleshot` on a fresh Homebrew installation requires core acceptance.

## Sources

- [Creating a tap](https://docs.brew.sh/How-to-Create-and-Maintain-a-Tap)
- [Adding software](https://docs.brew.sh/Adding-Software-to-Homebrew)
- [Formula acceptance](https://docs.brew.sh/Acceptable-Formulae)

Core requires a stable version, verifiable immutable source, open-source licensing,
a working supported-platform build and useful automated installation. A new project
can submit, but publication to a personal tap does not imply core acceptance.
