# Doubleshot

Keeps your Mac awake with the lid closed while a terminal session or command runs.
Closing the terminal or finishing the command restores normal sleep. The screen
locks and turns off when the lid closes.

Requires macOS 14 or later.

## Install

```sh
brew install jonathantybirk/tap/doubleshot
dshot
```

The first run installs the helper with administrator authentication.

## Usage

```sh
dshot                 # until Ctrl+C or the terminal closes
dshot -t 3600         # for one hour
dshot ./job.sh        # until the command finishes
dshot -w 12345        # until a process exits
```

`dshot` accepts caffeinate's `-dimsu`, `-t`, and `-w` options. Multiple sessions
keep the Mac awake until the last one ends. A launchd service restores sleep
if a client crashes; a separate recovery job handles a stalled service.

## Caffeinate shortcut

Run `dshot config init`, set `alias_caffeinate = true` in the printed file,
and add this to `~/.zshrc`:

```sh
eval "$(dshot shell-init zsh)"
```

Typed `caffeinate` commands then use Doubleshot. Bash and fish are also supported.

See `man dshot` for settings, status, and removal. Build with `make`; test with
`make test`. [MIT license](LICENSE).
