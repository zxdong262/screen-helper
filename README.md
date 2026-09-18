# screen-helper

[![build](https://github.com/zxdong262/screen-helper/actions/workflows/build.yml/badge.svg?branch=build)](https://github.com/zxdong262/screen-helper/actions/workflows/build.yml)
[![release](https://img.shields.io/github/v/release/zxdong262/screen-helper?sort=semver)](https://github.com/zxdong262/screen-helper/releases)
[![license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

[中文说明 →](README_CN.md)

External display management for macOS — extend, mirror, resolution, arrangement —
plus a repair path for the *"connected but offline"* state that Control Center
leaves behind when you pick **Stop Extending**.

Pure C11 on top of system frameworks. No third-party dependencies, no Xcode
project, no Swift runtime: `make` and you get one binary.

---

## The problem it fixes

Among the Control Center menu bar modules there is one whose icon is two
overlapping screens (the Screen Mirroring module). Open its dropdown, click
**Stop Extending**, and the external panel can end up in this state:

```
id=2 builtin=0 vendor=9747 model=1 online=0 active=0
```

The DisplayPort link is completely healthy — `Active = Yes`, `HPD = High`,
`DriverStatus = Ready` — so it is not the cable, the dock or the monitor. What
is stuck is a stale *independent output* display configuration left behind in
WindowServer / CoreGraphics.

The public call `SLSConfigureDisplayEnabled(config, id, true)` does nothing for
this state. The call that actually works is private, from SkyLight:

```c
SLSConfigureDisplayIndependentOutput(config, displayID, false)
```

`screen-helper fix` is built around exactly that call, with verification and
rollback around it.

Separately: when no external display is attached, that menu bar icon disappears
on its own. `screen-helper icon always` pins it back to always visible.

## Install

Grab the tarball from the [releases page](https://github.com/zxdong262/screen-helper/releases),
or take the latest one directly:

```bash
VERSION=1.0.0
curl -LO "https://github.com/zxdong262/screen-helper/releases/download/v$VERSION/screen-helper-v$VERSION-macos-universal.tar.gz"
curl -LO "https://github.com/zxdong262/screen-helper/releases/download/v$VERSION/screen-helper-v$VERSION-macos-universal.tar.gz.sha256"
shasum -a 256 -c "screen-helper-v$VERSION-macos-universal.tar.gz.sha256"

tar -xzf "screen-helper-v$VERSION-macos-universal.tar.gz"
./install.sh                        # -> /usr/local/bin  (asks for sudo)
PREFIX=~/.local ./install.sh        # install somewhere writable instead
```

The binary in the release is universal (`arm64` + `x86_64`) and ad-hoc signed.
`install.sh` clears the Gatekeeper quarantine attribute and re-signs it, which
is the part that makes a downloaded command line tool actually run.

From source:

```bash
make                 # native ./screen-helper, fastest for local hacking
make smoke           # build + read-only self checks
sudo make install    # -> /usr/local/bin
make dist            # universal tarball + sha256 in dist/
```

Requirements: macOS with the developer command line tools (for `clang`).
Nothing else. Built and tested on macOS 26.6.2 / Apple M1.

## Usage

```
screen-helper [command] [options]      # no command means `status`
```

Read-only:

- `status` — one-line-per-fact summary; the final `state` line tells you whether
  a repair is needed
- `list` — every display slot (`--all` includes ghost slots)
- `detail` — full field dump per display: EDID, serial, origin, pixel size
- `diag [--logs]` — system version, displays, DisplayPort link, menu bar
  preferences, private API availability

Mutating:

- `fix` — repair an offline external display, then restore the menu bar icon.
  This is the command the whole tool exists for
- `extend` — switch to extension mode (undo mirroring); `--arrange right|left|above|below`
  also places the display
- `mirror` — mirror the other online displays onto the main one
- `mode 2560x1440@60` — bind a resolution to the target display
- `enable <id>` / `disable <id>` — turn a display on or off; `disable` needs
  `--yes`, because it is the very thing that creates the broken state
- `save` — commit the current arrangement with `kCGConfigurePermanently`
- `icon always|auto|never|status` — manage the screen-mirroring menu bar icon

Choosing the target (defaults to the first external display):

- `--display 2` — by CoreGraphics display id
- `--edid 9747:1` — by EDID. **IDs change across replugs, EDIDs do not**, so
  prefer this one in scripts

General options:

- `--save` — apply to the current session only unless this is given
- `--dry-run` — print what would happen, change nothing
- `--force` — run the repair flow even if the display is already online
- `--no-icon` — leave the menu bar icon alone during `fix`
- `--all` — include ghost slots
- `-h`, `-V`

## Reproduce and repair

Click **Stop Extending** in that Control Center dropdown, wait for the external
panel to go dark, then:

```bash
./screen-helper status        # state line: external display present but OFFLINE
./screen-helper detail        # confirm online=0 active=0 while id / EDID are still there
./screen-helper fix           # repair, current session only
./screen-helper fix --save    # same thing, but persisted
./screen-helper extend        # if it came back mirrored, switch back to extension
```

`fix` runs in three stages, rescanning after each one instead of assuming any
single step succeeded:

1. `SLSConfigureDisplayIndependentOutput(cfg, id, false)`
2. if still offline, add an explicit `SLSConfigureDisplayEnabled` in the same
   transaction
3. if online but not active, bind a display mode onto it

Exit codes: `0` ok, `1` generic error, `2` usage, `3` no external display,
`4` private API unavailable, `5` ran but the panel is still offline.

## Why the repair writes blind

The private flag has **no getter**. There is no
`SLSGetDisplayIndependentOutput` or equivalent in SkyLight (checked symbol by
symbol with `dlsym`), and `SLSCopyDisplayInfoDictionary` does not expose the
field either. So the tool cannot read the flag's current value before writing
it, and cannot know in advance whether clearing it pulls the display back into
the display group or pushes it out.

So every commit is verified afterwards: all displays are rescanned, and if any
display that was online before the call went offline, the change is judged to
point the wrong way, inverted, and the run aborts. It would rather change
nothing than break a working screen. That step prints
`guard: no previously working display was affected`.

## Notes and caveats

- SkyLight is a private framework and its API comes with zero compatibility
  guarantees. After a system update, run `screen-helper diag` and read the
  `Private API` section — it tells you whether the symbols are still there.
- `CGDisplayVendorNumber` returns `0xFFFFFFFF` (not `0`) for an invalid display
  id, so validity checks have to exclude both values.
- macOS keeps a non-builtin *ghost* slot whose EDID is identical to the built-in
  panel (measured on this machine: `id=3`, `1552:41033`). Picking "the first
  non-builtin display" blindly hits that slot. The tool classifies it as
  `kind=ghost` and hides it by default.
- `disable` is the entry point to the broken state, hence the `--yes` gate.
- Last resort if a repair ever fails: replug the link, or restart WindowServer
  (which ends the graphical session). The tool prints that line when it gives up.

## Repository layout

Every file, in reading order.

Top level:

- [`Makefile`](Makefile) — build, smoke, universal, dist, install targets
- [`VERSION`](VERSION) — single source of truth for the version; injected into
  the binary as `SH_VERSION` and used by CI for the tag
- [`README.md`](README.md) — this file
- [`README_CN.md`](README_CN.md) — Chinese README
- [`LICENSE`](LICENSE) — MIT
- [`.gitignore`](.gitignore) — build output, `dist/`, local-only scratch dirs

Sources:

- [`src/main.c`](src/main.c) — argument parsing and dispatch; every subcommand
  is visible here at a glance
- [`src/commands.c`](src/commands.c) — the heavy one: `apply_steps`,
  `recover_display` (the three-stage repair), `apply_steps_guarded` +
  `snapshot_regressions` (the regression guard), and the per-command handlers
- [`src/commands.h`](src/commands.h) — option struct and command prototypes
- [`src/display.c`](src/display.c) — slot enumeration over ids 1..63,
  builtin/external/ghost classification, EDID-based target resolution, display
  transactions, mode selection
- [`src/display.h`](src/display.h) — display descriptor and transaction helpers
- [`src/skylight.c`](src/skylight.c) — two functions: one `dlopen`, cached
  `dlsym`; missing symbols return an error instead of crashing
- [`src/skylight.h`](src/skylight.h) — private API prototypes
- [`src/menubar.c`](src/menubar.c) — `ScreenMirroring` / `Display` menu bar
  preference keys; `ensure_visible` does not restart ControlCenter when the
  value is already right
- [`src/menubar.h`](src/menubar.h) — menu bar API
- [`src/diag.c`](src/diag.c) — diagnostics, including DisplayPort link state
  read straight from IOKit (no shelling out to `ioreg`)
- [`src/diag.h`](src/diag.h) — diagnostics API
- [`src/common.c`](src/common.c) — output helpers plus `sh_run` / `sh_capture`,
  which fork/execvp directly instead of going through a shell
- [`src/common.h`](src/common.h) — exit codes, `SH_VERSION` fallback, shared
  utilities

Automation:

- [`scripts/release`](scripts/release) — the release driver; pushes to the
  `build` branch that CI listens on
- [`scripts/install.sh`](scripts/install.sh) — shipped inside the release
  tarball; strips quarantine, ad-hoc signs, installs
- [`.github/workflows/build.yml`](.github/workflows/build.yml) — build, smoke
  test, tag and publish

## Development and releases

The version lives in [`VERSION`](VERSION) only. The Makefile injects it as
`-DSH_VERSION=...`, CI reads it for the tag, and nothing else stores a version
number.

Release flow — CI listens on the `build` branch and nowhere else, so publishing
is always an explicit act:

```bash
bash scripts/release --dry-run        # show what would happen
bash scripts/release                  # re-release the current VERSION
bash scripts/release --bump patch     # 1.0.0 -> 1.0.1, then release
bash scripts/release --version 2.0.0  # jump to an explicit version
```

What `scripts/release` does: preflight (clean tree, valid `VERSION`, not on
`build`) → sync with `origin` → optionally bump and commit `release vX.Y.Z` →
`make dist` locally to prove the thing still builds → push the source branch →
force-push `HEAD` to `build`.

CI then builds the universal binary with `-Werror`, smoke tests it, refuses to
publish if the tag already exists, and creates the GitHub release with the
`.tar.gz` and its `.sha256`. Normal commits to `main` trigger nothing.

## License

[MIT](LICENSE).
