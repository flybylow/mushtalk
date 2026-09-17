# Changelog

Tag a version whenever a firmware + conductor + sample combination works
together. The sketch ↔ form list is [docs/forms.md](docs/forms.md).

## v0.1 — browser remote, sound on the ESP32
- firmware: mushtalk_multi_v1 — LittleFS WAVs, Serial play/mute, BOOT mute, no auto-cycle
- conductor: USB serial bridge; Play / Audio off go to the microcontroller
- web: remote control only (no laptop speaker)
- samples: 6 mid-range WAVs (16-bit mono 44100) in `firmware/mushtalk_multi_v1/data/`
