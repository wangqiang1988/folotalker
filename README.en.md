<div align="center">

# FoloTalker

**Turn a FoloToy AI Passport into a walkie-talkie for your home.**

[User Manual](使用手册.md) · [Handoff](HANDOFF.md) · [Architecture](docs/architecture.md) · [HomeTalker](https://github.com/wangqiang1988/hometalker)

</div>

> The primary documentation for this repository is in Simplified Chinese.
> This file is the English summary; the
> [user manual](使用手册.md) and [handoff](HANDOFF.md) are the authoritative
> Chinese versions.

---

## What this is

[HomeTalker](https://github.com/wangqiang1988/hometalker) turns phones into
walkie-talkies inside your home Wi-Fi. **FoloTalker** is its device-side
client — a FoloToy AI Passport (ESP32-C3, 240×320 screen, three buttons)
joins the same room and talks with the phones.

**Hold the OK button to talk to your family, over your own home Wi-Fi.**

The device speaks the same WebSocket protocol as the phone client
(`join` / `speak` / `mute` plus 16 kHz mono Int16LE binary audio frames),
so both can be in the room at the same time.

| | Phone (HomeTalker) | Device (FoloTalker) |
| --- | --- | --- |
| How you talk | Hold the big round button on screen | **Hold the OK button** |
| Identity | Nickname + family code | Nickname + family code |
| Both in the room at once | Yes | Yes |

---

## Quick start

```bash
# 1. Build (ESP-IDF 5.5.3)
source ~/esp/esp-idf-v5.5.3/export.sh
./tools/validate.sh            # repo checks + host tests + firmware + merged image

# 2. Flash (merged image, at 0x0)
cd build/firmware/<sha256>/
python -m esptool --chip esp32c3 -b 460800 --before default_reset --after hard_reset \
  write_flash "@flash_args"

# 3. On the device: Settings -> Re-provision (use the official EspBlufi
#    app to send the Wi-Fi credentials)
#    -> enter server address -> nickname + family code -> hold OK to talk
```

Full steps: **[User Manual](使用手册.md)** (Simplified Chinese).

---

## Interface

All-Chinese interface, dark cards with an accent colour — deliberately
**not** the pixel-art shell from the upstream firmware's hardware-test menu.
Derivative applications must redesign their own UI.

```
┌────────────────────────────┐
│ Talk                 87%  │
│ ┌────────────────────────┐ │
│ │ Dad is talking         │ │
│ └────────────────────────┘ │
│ ┌────────────────────────┐ │
│ │ 3 online               │ │
│ │ Mom   Grandpa          │ │
│ └────────────────────────┘ │
│  ┌──────────────────────┐  │
│  │  HOLD TO TALK       │  │
│  └──────────────────────┘  │
│ Short press OK: settings   │
└────────────────────────────┘
```

Four screens: server address / gate / room / settings.

Button map (room screen):

| Button | Action |
| --- | --- |
| OK - hold | **Talk** |
| OK - short press | Open settings |
| OK - long press | Toggle microphone mute |
| Down - short press | Toggle microphone mute |

---

## Project structure

```text
main/
  ft_protocol.c   room protocol: JSON codec        [pure, host-tested]
  ft_state.c      connection + floor state machine [pure, host-tested]
  ft_store.c      encrypted NVS settings          [pure, host-tested]
  ft_textfield.c  on-screen keyboard              [pure, host-tested]
  ft_form.c       multi-row form                  [pure, host-tested]
  ft_glyphs.c     glyph coverage checking         [pure, host-tested]
  ft_texts.c      every UI string, in one place
  ----------------- no ESP-IDF / LVGL above this line -----------------
  ft_net.c        WebSocket transport
  ft_audio.c      16 kHz capture / playback
  ft_prov.c       BLE BLUFI provisioning
  ft_ui.c         application screens
  ft_theme.c      colour palette and font bindings
  ft_app.c        application controller
assets/fonts/     generated Chinese bitmap font subsets
tools/gen_fonts.py  font subset generator
```

**Design rule**: logic that does not need hardware must live in modules with
no ESP-IDF and no LVGL dependency. That is what lets host tests cover the
protocol parsing, floor timing, reconnect backoff, on-screen keyboard, and
glyph coverage — the parts most likely to break and hardest to spot on real
hardware.

---

## Chinese font coverage

LVGL's default Montserrat fonts contain **no Chinese glyphs**. Correct UTF-8
and a successful build both fail to prove that Chinese renders on screen.

This project:

1. Keeps every UI string in one place: `main/ft_texts.c`
2. Subsets Noto Sans CJK SC down to exactly those characters with
   `tools/gen_fonts.py`, then generates 16/20/28 px bitmaps via `lv_font_conv`
3. Fails the gate through `tests/test_font_coverage.py` if any character in
   the UI copy is missing from the generated fonts, and includes a
   known-missing negative case so the check cannot pass unconditionally

Currently **209 code points**, all three sizes covered. Missing glyphs render
as placeholder boxes rather than disappearing.
See the [font notes](assets/fonts/README.md).

---

## Security

| Concern | Handling |
| --- | --- |
| Family code / server address | Encrypted NVS partition (HMAC peripheral scheme, eFuse block 1) |
| Wi-Fi credentials | Encrypted NVS; BLE uses BLUFI's official security negotiation (DHM + AES + MD5) |
| Server certificate | Validated against the system certificate bundle; **there is no "skip verification" default** |
| Logging | Neither the family code nor the Wi-Fi password is ever logged or committed |

---

## Status

```
Build:        PASS
Host tests:   PASS
Device tests: NOT RUN     <- no real-device verification yet
```

Full status and the unverified list: **[Handoff](HANDOFF.md)**.

---

## Upstream and credits

| Project | Address |
| --- | --- |
| FoloToy AI Passport (firmware baseline, BSP, hardware docs) | https://gitee.com/FoloToy/ai-passport |
| HomeTalker (server + phone web client) | https://github.com/wangqiang1988/hometalker |

Built on the `main` branch of `ai-passport`, reusing its `components/bsp`
(display, buttons, audio, battery, shared I2C), but with the **application
layer and UI fully reimplemented**.

---

## Documentation

| Document | Contents |
| --- | --- |
| [使用手册](使用手册.md) | User manual (Simplified Chinese) |
| [HANDOFF.md](HANDOFF.md) | Handoff (Simplified Chinese) |
| [docs/architecture.md](docs/architecture.md) | Layering, threading, protocol details |
| [assets/fonts/README.md](assets/fonts/README.md) | Chinese font subset generation and checks |

---

## License

MIT, matching the upstream baseline. The Chinese font is Noto Sans CJK SC
under the SIL Open Font License 1.1.

See [NOTICE](NOTICE) for the sources, licences, and usage of every upstream
project and third-party component involved.
