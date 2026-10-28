# habits

A minimal habit tracker for the terminal. One C file, no dependencies,
plain-text state. Built for [iSH](https://ish.app) (Alpine Linux, i686)
on iPhone, runs on any POSIX terminal.

## How it works

On start, `habits` looks for today's file `$HABITS_DIR/YYYY-MM-DD.md`.
If there is none, it creates one from `$HABITS_CONFIG/template`,
prepending a `# YYYY-MM-DD` header.

Each habit gets a letter. Pressing it toggles the mark; `q` saves and
quits. While there are unsaved changes the header badge reads `UNSAVED`
and the `q` command turns red.

```
HABITS // 2026-10-28                UNSAVED

┌───┬────────────────────────────┬─────────┐
│ K │ HABIT                      │ STATE   │
├───┼────────────────────────────┼─────────┤
│ a │ exercise                   │ ■ DONE  │
│ b │ reading                    │ · TODO  │
│ c │ smoking                    │ · ----  │
│ d │ alcohol                    │ ■ LAPSE │
└───┴────────────────────────────┴─────────┘

a-d toggle  ·  q save & quit
```

The UI runs on the alternate screen and uses 24-bit color: a dark
background, one accent for done habits and one for lapses and unsaved
state. The table follows the terminal width up to 64 columns.

## File format

```
- good habit, not done
+ good habit, done
! bad habit, not done
* bad habit, done (a lapse)
```

The tracker records actions, not their absence: `*` means the bad habit
happened. A habit is any line starting with one of `-+!*` followed by a
space. Every other line (headers, notes, blank lines) is kept byte for
byte, so day files can be edited by hand.

Up to 25 habits get keys (`a`–`z` without `q`), assigned by position.

## Environment

| Variable        | Meaning                        | Default |
|-----------------|--------------------------------|---------|
| `HABITS_CONFIG` | directory containing `template` | `.`     |
| `HABITS_DIR`    | directory for day files         | `.`     |
| `TZ`            | timezone for the current date   | system  |

Neither directory is created automatically.

## Build

Native (on iSH: `apk add build-base`):

```sh
make
```

Cross-compile a static i686 musl binary with [zig](https://ziglang.org):

```sh
make cross
```

The binary is built with `-mcpu=i686`, so it contains no SSE
instructions, which iSH does not fully emulate.

`make deploy` cross-compiles and copies the binary to `DEST`
(an iCloud Drive folder by default; override with `make deploy DEST=...`).

## iSH notes

iSH defaults to UTC, so the day would roll over at the wrong hour.
Set the timezone in `~/.profile`; musl accepts POSIX TZ strings without
`tzdata` (the sign is inverted: `UTC-3` means UTC+3):

```sh
export TZ=UTC-3
export HABITS_CONFIG=/mnt/icloud/habits
export HABITS_DIR=/mnt/icloud/habits/days
```

Changes are written only on `q`. If iOS suspends and kills iSH with
unsaved changes, they are lost. `^C` quits without saving.

## License

MIT, see [LICENSE](LICENSE).
