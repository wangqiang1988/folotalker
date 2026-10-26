<p align="right">
  <a href="architecture.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# FoloTalker Architecture

How the device-side client is put together, and why.

## Layering

The rule that shapes the whole codebase: **logic that does not need hardware
lives in modules with no ESP-IDF and no LVGL dependency.** That is what makes
the protocol codec, the floor state machine, the reconnect backoff, the
on-screen keyboard, and the glyph coverage testable on a host machine.

```text
main/
  ft_protocol.c   room protocol: JSON encode / decode      [pure]
  ft_state.c      connection + floor state machine          [pure]
  ft_store.c      encrypted NVS settings                    [pure]
  ft_textfield.c  on-screen keyboard logic                  [pure]
  ft_form.c       multi-row form logic                      [pure]
  ft_glyphs.c     glyph coverage checking                   [pure]
  ft_texts.c      every UI string, in one place
  ───────────────────── no ESP-IDF / LVGL above this line ─────────────────────
  ft_net.c        WebSocket transport
  ft_audio.c      16 kHz capture and playback
  ft_prov.c       BLE BLUFI Wi-Fi provisioning
  ft_ui.c         application screens
  ft_theme.c      colour palette and font bindings
  ft_app.c        application controller
components/bsp/   borrowed unchanged from the upstream baseline
```

## Threading

| Thread | Responsibility |
| --- | --- |
| `ft_app` | **Sole owner of the application state.** Drains network events, applies key events, drives reconnect backoff. |
| `ft_ui` | LVGL objects. Renders the state, posts key events back. |
| `ft_ws` | WebSocket transport. Its callback only copies data into queues. |
| `ft_cap` | Audio capture: reads the codec, sends 20 ms frames. |
| `ft_play` | Audio playback: writes the codec. |

LVGL is not thread-safe, so every access from outside the LVGL task holds
`bsp_lvgl_lock()`.

The split matters: the WebSocket callback runs in the component's own task
and must never block, so it does nothing but copy bytes into a queue. The
button callback runs in a shared `esp_timer` task and does the same.

## Protocol

Identical to the HomeTalker server
([`server/src/index.ts`](https://github.com/wangqiang1988/hometalker/blob/main/server/src/index.ts)).

### Control messages — JSON text frames

```json
{"t":"join","name":"爸爸","code":"1234"}
{"t":"speak","on":true}
{"t":"mute","on":true}
{"t":"ping"}
```

### Server events — JSON text frames

```json
{"t":"welcome","you":{"id":"...","name":"..."},"room":{"name":"home"},
 "members":[{"id":"...","name":"...","muted":false,"speaking":false}],
 "speaker":{"id":"...","name":"..."},
 "cfg":{"sampleRate":16000,"frameMs":20,"maxSpeakMs":60000,
        "halfDuplex":true,"speakMode":"preempt"}}
{"t":"members","members":[...]}
{"t":"speaker","id":"...","name":"..."}
{"t":"error","code":"BAD_CODE","message":"..."}
```

### Audio — binary frames

16 kHz, mono, Int16 little-endian, exactly 20 ms per frame (320 samples,
640 bytes). The device configures the ES8311 to run natively at 16 kHz, so
there is no software resampling.

### Why a hand-written JSON scanner

The protocol only needs a handful of fields from a few fixed event shapes.
`cJSON` or `json-c` would add a dependency and code size to a board with
8 MB of Flash and no PSRAM. `ft_protocol.c` uses a small directional scanner
instead — about 250 lines, and it is fully covered by host tests.

One subtlety worth remembering: **always cast to `unsigned char` before
comparing against control-character boundaries.** A `signed char` holding a
UTF-8 continuation byte is negative, so `c < 0x20` would be true and the
byte would be escaped as `\u00xx`, mangling every Chinese character.

## Floor (microphone) control

The server is the single source of truth for who holds the floor.

```text
user presses OK
        │
        ├─► send {"t":"speak","on":true}
        │         │
        │         ├─ server grants  ──► speaker event says "you"
        │         │                        └─► stream audio
        │         │
        │         └─ server refuses (exclusive mode)
        │                  └─► error FLOOR_BUSY, show the hint
        │
        └─ user releases OK
                  └─► send {"t":"speak","on":false}, stop capturing
```

If the server gives the floor to somebody else while the button is held, the
device **stops sending immediately**. The `SPEAKER` event is authoritative:
whenever the floor is not ours, both the `ACTIVE` and `REQUESTED` states are
reset. Continuing to stream would waste power and bandwidth on audio the
server discards.

## Reconnection

`ft_state.c` owns an exponential backoff: 1 s → 2 s → 4 s → 8 s → 10 s (capped).

- A rejected join (wrong family code, room full, locked out) **stops the
  reconnect loop** and returns to the gate screen. Retrying would only lock
  the client out harder.
- Any other disconnect schedules a reconnect.
- If Wi-Fi is down when the timer fires, the reconnect is postponed rather
  than hammered.

## Storage

| Key | Contents |
| --- | --- |
| `host` | HomeTalker host name or IP |
| `name` | Nickname |
| `code` | Family code |
| `volume` | Playback volume, 0–100 |
| `wifi_ssid` / `wifi_pass` | Wi-Fi credentials (written by the provisioning module) |

All in the **encrypted NVS partition** (`CONFIG_NVS_ENCRYPTION` with the
HMAC peripheral scheme, eFuse block 1). The eFuse key is generated on first
boot and cannot be changed afterwards, so the same data on a different board
cannot be decrypted — that is the intended behaviour.

Wi-Fi passwords are never logged and never committed.

## Chinese font coverage

LVGL's default Montserrat fonts contain no Chinese glyphs. Correct UTF-8 and
a successful build both fail to prove that Chinese renders on screen.

The pipeline:

1. `main/ft_texts.c` — the single source of truth for UI copy
2. `tools/gen_fonts.py` — subsets Noto Sans CJK SC to exactly those
   characters, verifies each requested code point actually exists in the
   source font, then runs `lv_font_conv` at 16/20/28 px, 4 bpp
3. `tests/test_font_coverage.py` — fails the build if any character in the
   UI copy is missing from the generated fonts, and includes a known-missing
   negative case so the check cannot pass unconditionally

Currently 209 code points, all three sizes covered. Missing glyphs render as
placeholder boxes rather than disappearing, and Montserrat stays as the
fallback for LVGL's built-in icons.

## Security posture

| Concern | Handling |
| --- | --- |
| Family code, server address | Encrypted NVS |
| Wi-Fi credentials | Encrypted NVS; BLE side uses BLUFI's official security negotiation (DHM + AES + MD5) |
| Server certificate | Validated against the system certificate bundle; **there is no "skip verification" default** |
| Logs | Neither the family code nor the Wi-Fi password is ever logged |

## Hardware reuse

`components/bsp` is borrowed from the upstream baseline unchanged. The
application never duplicates a pin constant, an I2C address, or a register
value — those all live in `components/bsp/include/bsp_pins.h`, which is the
single source of truth for this board.

The application adds no new hardware interfaces and no new wiring.
