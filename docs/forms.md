# Sketch ↔ form index

Which Arduino sketch is flashed on which ceramic (or bench). Fill a row when a piece gets a board, and **tag** that moment so we can check out the exact firmware + samples later.

## How to use

1. One row per physical form (or bench rig).
2. `sketch` is the folder under `firmware/` that was uploaded.
3. `tag` is a git tag on the commit that actually ran on that piece (`v0.1`, later `form-academy-v1`, …).
4. When a form needs its own behaviour, **copy** the sketch into a new folder named after the form (`firmware/academy/`, `firmware/home/`, …). Do not overwrite `mushtalk_multi_v1` in place if another form still uses it.
5. After a known-good flash:

       git tag -a form-<slug>-v1 -m "academy vessel — mushtalk_multi_v1"
       git push origin form-<slug>-v1

   Then add/update the row below.

## Index

| form | sketch | tag | flashed | notes |
| --- | --- | --- | --- | --- |
| USB bench (Mac + speaker) | `firmware/mushtalk_multi_v1` | `v0.1` | 2026-09-17 | First working: browser remote → conductor USB serial → ESP32-S3 + MAX98357A. Mute via BOOT or Audio off. |
| *(next ceramic)* | | | | name the piece, copy or reuse the sketch, tag after it plays |

## Sketch folders

| folder | role |
| --- | --- |
| `firmware/mushtalk_multi_v1/` | Current player (LittleFS WAVs, serial + mute button). Bench until a form owns a copy. |
