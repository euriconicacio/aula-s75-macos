# aula-s75-macos

Use the AULA S75 Pro keyboard on macOS without the vendor's Windows-only software:

- **`aula-s75`**: a small command-line tool that sets the clock on the keyboard's screen
  and reads its battery level, over the USB cable or the 2.4 GHz dongle. Plain C on IOKit,
  no dependencies.
- **`web/`**: a container that serves [open-aula](https://github.com/paulcoding810/open-aula),
  a browser configurator for lighting, per-key colours, keymaps, macros, screen images and
  settings, on localhost.

> Unofficial. This project has no affiliation with AULA. It writes to the keyboard's
> configuration using a reverse-engineered protocol. Use it at your own risk.

## Requirements

- macOS with the Xcode Command Line Tools (`xcode-select --install`); tested on Apple silicon
- An AULA S75 Pro (USB `0c45:800a`, dongle `05ac:024f`)
- For the configurator: Docker, and a Chromium-based browser (Chrome, Edge, Brave, Arc).
  Safari and Firefox do not support WebHID.

Other AULA models are not supported, even when they share the USB IDs.

## `aula-s75`

### Build and install

```sh
make
make install            # installs to ~/.local/bin; use PREFIX=/usr/local to change
```

### Usage

```sh
aula-s75 clock          # set the screen clock to the Mac's local time
aula-s75 battery        # read the battery level (2.4G mode only)
```

`clock` tries the USB cable first, then the dongle. To pick one, add `--usb` or `--dongle`.
`--dry-run` prints the packets without sending them, and `-v` prints every packet sent and received.

| Exit code | Meaning |
|---|---|
| 0 | done |
| 1 | the keyboard refused the command or did not answer |
| 2 | usage error |
| 3 | keyboard not found |

### Things to know

- **The battery level is only reported over 2.4 GHz.** On the cable the keyboard is treated
  as charging and sends no level.
- **Over 2.4 GHz the keyboard does not answer while it is asleep.** Press a key, then run the
  command again.
- **A reading of 0% can mean "no reading"**, not only an empty battery. The level is
  estimated from the battery voltage by the keyboard itself, so no software can correct it.
  If the keyboard works wirelessly and still reads 0%, charge it fully from a port on the
  Mac or a plain 5 V charger, not through an unpowered hub.

### Keep the clock in sync

The keyboard keeps its own time. To set it at login and every 6 hours:

```sh
make install
sed "s|__BIN__|$HOME/.local/bin/aula-s75|" contrib/aula-s75.clock.plist \
  > ~/Library/LaunchAgents/aula-s75.clock.plist
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/aula-s75.clock.plist
```

To remove it:

```sh
launchctl bootout gui/$(id -u)/aula-s75.clock
rm ~/Library/LaunchAgents/aula-s75.clock.plist
```

## Web configurator

```sh
cd web
docker compose up -d --build
```

Open <http://127.0.0.1:8765> in a Chromium-based browser, click **Connect device** and pick
`AULA-S75Pro`. Use the USB cable: screen uploads need it, and most other settings also work
over 2.4 GHz. To stop it, run `docker compose down`.

The container only serves static files. The browser on the Mac talks to the keyboard through
WebHID, so the container has no USB access. Docker Desktop on macOS could not provide it anyway.

The container's settings:

- The port is published on `127.0.0.1` only.
- It runs as non-root nginx, with a read-only filesystem, all capabilities dropped and
  `no-new-privileges`.
- A Content Security Policy allows the page to load only from its own origin.

open-aula has no license, so this repository does not include its code. The image build
fetches it from upstream, pinned to the commit in `web/Dockerfile`
(`OPEN_AULA_COMMIT`), and checks that it got that exact commit. That commit was reviewed
before it was pinned: it makes no network requests, loads nothing from other origins,
stores its settings only in the browser's `localStorage`, and has no firmware-update or
reset commands for the S75 Pro. If you move the pin, review the new code first.

## Protocol

See [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Credits

The protocol was reverse-engineered by:

- [paulcoding810/open-aula](https://github.com/paulcoding810/open-aula), from the vendor's
  Windows driver
- [w33ble/aula-f75-max-control](https://github.com/w33ble/aula-f75-max-control)
- [livvaa/AulaToolkit](https://github.com/livvaa/AulaToolkit)

## License

MIT, for the code in this repository. See [LICENSE](LICENSE). open-aula is not covered by
this license.
