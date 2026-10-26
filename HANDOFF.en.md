<div align="center">

# Handoff · FoloTalker

> Written for whoever picks this up next (including future me).
> Last updated: 2026-10-26
> Repository: https://github.com/wangqiang1988/folotalker
> Upstream baseline: https://gitee.com/FoloToy/ai-passport

</div>

> The authoritative handoff is in Simplified Chinese: **[HANDOFF.md](HANDOFF.md)**.
> This file is the English summary.

---

## 1. Where things stand

**The firmware is functionally complete, compiles cleanly, and passes every
host test and repository gate — but it has never run on real hardware.**

The code lives on `feature/folotalker-device` (based on `ai-passport`'s
`main` @ a06235e). The deliverable is a verified merged image,
`build/FoloToy-AI-Passport-full.bin`, flashable from `0x0`.

**Next step: plug the device in, flash it, and look at the screen.**

---

## 2. What is done and verified

| Module | Status | Host test |
| --- | --- | --- |
| `main/ft_protocol.c` | Room protocol JSON codec (hand-written scanner, no cJSON) | yes — `test_ft_protocol.c` |
| `main/ft_state.c` | Connection + floor state machine, exponential backoff | yes — `test_ft_state.c` |
| `main/ft_store.c` | Encrypted NVS settings, host / code / URL validation | yes — `test_ft_store.c` |
| `main/ft_textfield.c` | On-screen keyboard (charset switch, cursor, backspace) | yes — `test_ft_textfield.c` |
| `main/ft_form.c` | Multi-row form (row switching must not lose text) | yes — `test_ft_form.c` |
| `main/ft_glyphs.c` | Glyph coverage checking (UTF-8 decode + codepoint query) | yes — `test_ft_glyphs.c` |
| `main/ft_net.c` | WebSocket transport (esp_websocket_client 1.8.0) | **no — needs hardware** |
| `main/ft_audio.c` | 16 kHz capture / playback tasks | **no — needs hardware** |
| `main/ft_prov.c` | BLE BLUFI provisioning + Wi-Fi credential storage | **no — needs hardware + phone** |
| `main/ft_ui.c` | Four screens and navigation | **no — needs hardware** |
| `main/ft_app.c` | Application controller (sole owner of the state) | **no — needs hardware** |

Roughly 7,600 new lines including tests and the font generator.

### Validation results (last full run, all green)

```
Repository checks        PASS   (178 text files)
Key-wake guard tests     PASS
BSP button fault inject  PASS
BSP LVGL init            PASS
BSP audio fault / retry  PASS
FoloTalker host tests    PASS  (protocol/state/store/keyboard/form/glyphs, 6 files)
Chinese font coverage    PASS   (3 sizes x 209 codepoints, negative case valid)
Repo-check self tests    PASS
Host tests               PASS
Firmware build           PASS   (merged 1,924,608 B, flash at 0x0)
```

> The baseline repository's demo-screen tests (`test_ui_pixel_math`,
> `test_demo_navigation`, `test_demo_*_runtime`, `test_deep_sleep_contract`)
> were removed together with the demo sources they covered - that code is the
> hardware-capability test UI, and this project does not use it.

### Deliverables

> The sha256 **changes on every build** — the app image embeds a build
> timestamp. Do not hardcode a hash in documentation; query the current
> firmware's identity with the command below.

```
build/FoloToy-AI-Passport-full.bin
  sha256: see manifest.json (differs per build, see note)

build/firmware/<sha256>/
  FoloToy-AI-Passport-full.bin   merged image
  FoloToy-AI-Passport.bin        app (0x10000)
  FoloToy-AI-Passport.elf        symbols
  FoloToy-AI-Passport.map        memory map
  bootloader/bootloader.bin      (0x0)
  partition_table/partition-table.bin  (0x8000)
  flash_args                     esptool arguments
  manifest.json                  per-file sha256
```

Verify with:
`python3 tools/archive_firmware.py verify build/firmware/<sha256>/`

### Size

```
app        1,859,072 B / 8,323,072 B  factory partition  (78% free)
DRAM         213,008 B /   321,296 B                    (66%)
Flash Code  1,335,780 B
Flash Data    389,948 B
```

The Chinese fonts account for about 682 KB of Flash (16/20/28 px, 4 bpp,
uncompressed).

---

## 3. Environment facts (probed — do not redo)

### Dev machine

| Item | Finding |
| --- | --- |
| ESP-IDF | **v5.5.3**, at `~/esp/esp-idf-v5.5.3` (not `/opt`) |
| Activate with | `source ~/esp/esp-idf-v5.5.3/export.sh` |
| Python | IDF venv at `~/.espressif/python_env/idf5.5_py3.12_env` |
| Toolchain | `riscv32-esp-elf` gcc 14.2.0 under `~/.espressif/tools/` |
| `lv_font_conv` | installed with npm into `/tmp/opencode/node_modules/.bin/` (not on PATH) |
| `fontTools` | `pip install --break-system-packages fonttools` (system Python has PEP 668) |
| Noto CJK | `/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc` (sub-font index 2 = SC) |
| USB device | **No ESP32-C3 attached** (`lsusb` shows no 303a:xxxx, no `/dev/ttyACM*`) |

### Required skills

The five `passport-*` skills live in `~/.config/opencode/skills/`, copied from
the `ai-passport` repository's `skills/` directory. **Reinstall them on a new
machine.**

### HomeTalker server

At `~/Repositories/hometalker`, running on `10.255.1.101:8443` (Caddy
reverse-proxying 443). **This machine (`10.255.1.98`) has no Docker
permission.** `hometalker/HANDOFF.md` has the full server port map and the TLS
troubleshooting conclusions — do not redo that work.

---

## 4. Architecture and key decisions

### Layering

```
main/
  ft_protocol.c   room protocol: JSON codec              [pure]
  ft_state.c      connection + floor state machine       [pure]
  ft_store.c      encrypted NVS settings                 [pure]
  ft_textfield.c  on-screen keyboard                    [pure]
  ft_form.c       multi-row form                        [pure]
  ft_glyphs.c     glyph coverage checking               [pure]
  ft_texts.c      every UI string, in one place
  ----------------- no ESP-IDF / LVGL above this line -----------------
  ft_net.c        WebSocket transport
  ft_audio.c      16 kHz capture / playback
  ft_prov.c       BLE BLUFI provisioning
  ft_ui.c         application screens
  ft_theme.c      colour palette and font bindings
  ft_app.c        application controller
components/bsp/   borrowed unchanged from the upstream baseline
```

**Core rule**: hardware-independent logic lives in modules with no ESP-IDF or
LVGL dependency, so it can be covered by host tests. The protocol codec, floor
timing, backoff calculation, keyboard, and font coverage — the pieces most
likely to break and hardest to notice on hardware — all sit in that layer.

### Threading

| Thread | Responsibility |
| --- | --- |
| `ft_app` | **Sole owner of the application state.** Drains network events, applies key events, drives backoff |
| `ft_ui` | LVGL objects. Renders state, posts key events |
| `ft_ws` | WebSocket transport; its callback only copies data into queues |
| `ft_cap` | Audio capture: reads the codec, sends 20 ms frames |
| `ft_play` | Audio playback: writes the codec |

LVGL is not thread-safe, so every access from outside the LVGL task holds
`bsp_lvgl_lock()`.

### Decisions and why

| Decision | Reason |
| --- | --- |
| Hand-written JSON scanner instead of cJSON | The protocol needs a handful of fields from a few fixed event shapes; adding a dependency to a no-PSRAM C3 is not worth it |
| Keys use PRESS/RELEASE, not CLICK | "Hold to talk" must start streaming on press-down; waiting for CLICK adds 180 ms of latency |
| The server owns the floor | The server is the single source of truth; streaming locally while preempted sends audio somebody else is using |
| Wi-Fi credentials in encrypted NVS, not `WIFI_STORAGE_FLASH` | `esp_wifi_set_storage` stores plaintext; credential-class data must be encrypted |
| Font subset at 4 bpp, uncompressed | Keeps the generated C readable and avoids a decoder mismatch; there is 78% Flash headroom anyway |
| Kept the baseline `key_guard.h` press guard | After a deep-sleep key wake the button is still held, which fires an unintended long-press action. That is a real bug, not a style choice |

### The UI redesign

The upstream `main` branch's demo menu and `ui_pixel` pixel shell are
**hardware-capability test screens**, not an application UI, and derivative
applications must not reuse them. This project is a **from-scratch design**:

- Dark cards with an accent colour (no pixel art)
- Four screens: server address / gate / room / settings
- Room layout: title bar (battery top-right) -> speaker status card ->
  members card -> large hold-to-talk button -> hint row
- Palette in `main/ft_theme.h`

---

## 5. Pitfalls hit, and what they taught

### 1. `char c < 0x20` mangles UTF-8 on signed char

While writing JSON escaping I used `if (c < 0x20)` to detect control
characters. UTF-8 bytes of Chinese characters are negative in a signed char,
so **every byte was treated as a control character and escaped as `\u00xx`**,
turning all Chinese into garbage.

**Lesson**: always cast to `unsigned char` before classifying a character.

```c
unsigned char c = (unsigned char)src[i];
if (c < 0x20) { /* actually a control character */ }
```

### 2. Using a boolean reader for a string field

`speakMode` is the string `"preempt"` / `"exclusive"`, not a boolean. I read
it with `get_bool` first; the function returned false, the whole branch was
skipped, and `exclusive` was permanently false.

**Lesson**: confirm the field's actual JSON type before choosing a reader.

### 3. The backoff time base overwrote itself

`ft_state_tick()` wrote `st->now_ms = now_ms` and then computed elapsed time
as `now_ms - st->now_ms` — always 0, so the backoff never fired.

**Lesson**: keep the start point in its own field (`retry_deadline_ms`);
do not share a variable between "current time" and "when the timer started".

### 4. Kept streaming audio after being preempted

When the server gave the floor to somebody else, the local `speak` state was
still `REQUESTED`, so the device kept pushing audio (the server discarded it,
but it wasted power and bandwidth).

**Lesson**: the server's SPEAKER event is authoritative. Whenever the floor
is not ours, reset both `ACTIVE` and `REQUESTED`.

### 5. The IDF 5.5 BLUFI API differs completely from the old one

I wrote against the `demo/blufi-provisioning` branch (old API) and got a wall
of compile errors:

| What I wrote (old API) | What IDF 5.5 actually has |
| --- | --- |
| `esp_blufi_svc_init(&callbacks)` | `esp_blufi_register_callbacks()` + `esp_blufi_profile_init()` |
| `callbacks.negotiate` | `callbacks.negotiate_data_handler` |
| `mbedtls_dhm_calc_pubkey()` | `mbedtls_dhm_make_public()` |
| `esp_blufi_report_error()` | `btc_blufi_report_error()` (extern declaration) |
| `esp_netif_init()` returns esp_err_t | returns **void** |
| `nimble_port_run()` returns esp_err_t | returns **void** |
| `ble_hs_cfg.gap_event_cb` | this NimBLE version has **no such field** |

**Lesson**: the authoritative reference for IDF 5.5 is
`~/esp/esp-idf-v5.5.3/examples/bluetooth/blufi/main/`. Copy the structure
from there; do not copy from older branches.

### 6. ESP32-C3 has no Flash Encryption

Enabling `CONFIG_NVS_ENCRYPTION=y` fails to compile:

```
#error "NVS Encryption (HMAC): Configured eFuse block out of range!"
```

The C3 has no Flash Encryption, so NVS encryption must use the **HMAC
peripheral** scheme:

```ini
CONFIG_NVS_ENCRYPTION=y
CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC=y
CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=1
```

**Note**: first boot writes the HMAC key into eFuse block 1, and **that step
is irreversible**. Data from another board cannot be decrypted — which is
the intended behaviour. E Fuse block 1's purpose is
`ESP_EFUSE_KEY_PURPOSE_HMAC_UP`.

### 7. LVGL 9.6 API changes

| Old | New |
| --- | --- |
| `lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN)` | `lv_obj_set_hidden(obj, true)` |
| `lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN)` | `lv_obj_set_hidden(obj, false)` |
| `lv_timer_create(cb, period)` | `lv_timer_create(cb, period, user_data)` |
| `CONFIG_LV_MEM_SIZE_KILOBYTES=48` | `CONFIG_LV_MEM_SIZE=49152` (bytes) |

### 8. `esp_websocket_client_send_text` needs a timeout argument

The IDF 5.5 signature is `send_text(client, data, len, timeout)`; omitting it
does not compile.

### 9. NVS accessor types

`nvs_get_i32()` wants an `int32_t *`. Passing an `int *` directly raises
`-Wincompatible-pointer-types`, which is a hard error under `-Werror=all`.

### 10. The upstream gate script does not fit a standalone app repo

`ai-passport`'s `tools/check_repo.py` requires every `.md` to have a
`.zh_CN.md` pair, forbids Chinese characters in the default `.md`, and
requires `.github/ISSUE_TEMPLATE/`. Those are the upstream baseline
repository's conventions.

This repository is a standalone application repository whose documentation is
Chinese-first, so it ships its own `tools/check_repo.py`. It still checks that
link targets exist, that no credentials or build artifacts are committed, that
every application module is registered in `CMakeLists.txt`, and that the
fonts are registered exactly once — but it does not force bilingual pairing.

---

## 6. Next steps

### Doable right now (no new decisions needed)

1. **Attach the device and flash it**
   ```bash
   cd ~/Repositories/folotoytalker
   source ~/esp/esp-idf-v5.5.3/export.sh
   python -m serial.tools.list_ports -v      # find the port first
   # once the port is confirmed:
   idf.py -B build -p /dev/ttyACM0 flash
   ```
2. **Watch the serial log**: `idf.py -B build -p /dev/ttyACM0 monitor`.
   Look for `FoloTalker 启动完成` and any crash backtrace.
3. **Look at the screen**: do all four screens render, and is the Chinese
   text showing as boxes (it should not be)?

### Needs outside help

4. **Run the HomeTalker server** (or use the existing
   `10.255.1.101:8443`), then enter the address, nickname, and family code on
   the device and see whether it joins the room.
5. **Test BLE provisioning for real**: install the official EspBlufi app and
   provision the device's Wi-Fi.
6. **End-to-end talk test**: phone and device in the same room, talking to
   each other; measure latency and audio quality.

### Might need changes, depending on what the measurements show

7. **WSS certificate validation**: HomeTalker uses a self-signed CA. The
   device validates against the system certificate bundle and has **no
   "skip verification" switch**. If the handshake fails, decide between:
   - baking the CA certificate into the firmware (`cert_pem` in `ft_net.c`)
   - adding an "insecure" setting (lowers security; needs explicit consent)
8. **Audio latency and quality**: 16 kHz is required by the protocol; the
   ES8311 gain and I2S buffer depth may need tuning.
9. **Overlong member nicknames**: currently truncated; may need shortening
   display instead.

---

## 7. Command cheat sheet

```bash
# Environment
source ~/esp/esp-idf-v5.5.3/export.sh

# Complete gate (repo checks + host tests + firmware + merged image verification)
./tools/validate.sh

# Host tests only (fast, seconds)
./tools/validate.sh --static

# Firmware build only
./tools/validate.sh --firmware

# Run one host test
cc -std=c11 -Wall -Wextra -Werror -Imain \
   tests/test_ft_state.c main/ft_state.c main/ft_protocol.c \
   -o /tmp/t && /tmp/t

# Regenerate the Chinese fonts (after editing ft_texts.c)
pip install --break-system-packages fonttools
npm install lv_font_conv          # when needed
PATH="/tmp/opencode/node_modules/.bin:$PATH" python3 tools/gen_fonts.py
python3 tests/test_font_coverage.py     # must pass

# Verify the firmware bundle
python3 tools/archive_firmware.py verify build/firmware/<sha256>/

# Memory usage
idf.py -B build size
idf.py -B build size-components
```

---

## 8. Unverified items

None of the following has been checked on real hardware. Report them as
`Unverified`:

| Item | Why not verified |
| --- | --- |
| On-screen Chinese rendering | No device attached |
| Button feel and response latency | No device attached |
| Microphone capture quality / speaker audio quality | No device attached |
| BLE BLUFI provisioning with the official app | No device, no phone app |
| WSS handshake (self-signed CA validation) | No device attached |
| End-to-end interop with the HomeTalker server | No device attached |
| NVS encryption generating the eFuse key on first boot | No device attached |
| Automatic reconnect after a power cycle (Wi-Fi + server) | No device attached |
| Preempt and exclusive floor modes | No device attached |
| Audio latency | No device attached |
| Deep-sleep wake (this application does not enable deep sleep) | No device attached |

**A successful build is not hardware validation.** Every item above needs to
be exercised on a real device.

---

## 9. Related repositories

| Repository | Address |
| --- | --- |
| FoloTalker (this project) | https://github.com/wangqiang1988/folotalker |
| HomeTalker (server + phone client) | https://github.com/wangqiang1988/hometalker |
| FoloToy AI Passport (firmware baseline) | https://gitee.com/FoloToy/ai-passport |
