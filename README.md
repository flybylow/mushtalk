# mushtalk

Networked ceramic-audio installation. Sound comes from ESP32-S3 nodes
(MAX98357A + speaker), not from the laptop. The browser is a remote control.

## Layout

    conductor/   Node.js — web UI + USB serial bridge to the ESP32
    web/         browser remote (Play / Audio off → microcontroller)
    firmware/
      mushtalk_multi_v1/          Arduino sketch + data/ (WAVs for LittleFS)
    docs/forms.md                 which sketch is flashed on which ceramic
    samples live once, in firmware/mushtalk_multi_v1/data/

## Run (browser controls the board)

Plug the ESP32 in over USB, then:

    cd conductor
    npm start                                  # http://localhost:8080

Open the URL, pick a sample, Play. Sound comes from the ESP32 speaker.
Before an Arduino upload, click **Release USB to flash** (the site stays on
8080). After the sketch lands, **Reconnect**. Override the serial device with
`SERIAL_PATH=/dev/cu.usbmodemXXXX npm start`.

## Firmware

Open firmware/mushtalk_multi_v1/ in Arduino IDE.
Board: ESP32S3 Dev Module. PSRAM: OPI PSRAM. USB CDC On Boot: Enabled.
Upload the LittleFS image (data/) once, then the sketch. The board waits for
commands over Serial (115200): sample number/name, `on`/`off`/`m`, `stop`, `c`.

Mute: press the onboard BOOT button (GPIO0) to toggle sound on/off — output
stops immediately. Serial `on` / `off` / `m` do the same. For a panel button,
wire a switch between GPIO4 and GND and set `MUTE_BTN` to 4 in the sketch.

## Versioning

Plain Git. Tags mark known-good flashes. **Which sketch is on which ceramic**
is listed in [docs/forms.md](docs/forms.md) — add a row when a form gets a board.

    git tag -a v0.1 -m "browser remote, sound on the ESP32"
    git push origin v0.1

Check out that tag later and you get the exact code (and samples) that ran.
When a vessel needs its own sketch, copy `firmware/mushtalk_multi_v1/` to a
folder named after the form and update the table.
