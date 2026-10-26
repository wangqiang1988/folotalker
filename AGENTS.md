<p align="right">
  <a href="AGENTS.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Repository Guidelines for AI Agents

This file is the mandatory entry point for AI-assisted work in this repository.
Read the task-specific documents from the routing table below; do not load every
README by default.

## Required AI skills

The following five skills are required for AI-assisted development here:
`passport-develop`, `passport-setup`, `passport-build`, `passport-device-test`,
and `passport-debug`. Their maintained sources are the `skills/` directory of the
upstream baseline repository,
[ai-passport](https://gitee.com/FoloToy/ai-passport).

Before starting development, the AI must check that all five are installed and
available in the current environment. If any are missing, the AI must choose an
appropriate installation method for its tool and environment, perform the
installation, and verify availability itself. Do not wait for the user to
request installation or make the user choose the method or run installation
commands. No particular script, installation directory, or AI client is
mandatory.

Respect the environment's approval requirements and preserve existing skills
and user configuration. If permissions, conflicts, or platform limitations
prevent installation, explain the blocker and request only the necessary user
action; do not claim installation succeeded. Having all five available does not
mean loading or invoking all five for every task: use only the matching skills,
and retain the separate authorization requirements for flashing, Git writes, and
publishing.

## Project baseline

- Target: ESP32-C3, 8 MB Flash, no PSRAM, ESP-IDF 5.5.3.
- Board support (`components/bsp`) is borrowed from the upstream baseline and is
  the single source of truth for pins, addresses, and peripheral parameters.
  Do not duplicate those constants in application code.
- The application layer and the entire UI are original work. The upstream
  baseline's demo menu and pixel-art shell are hardware-test screens and must
  not be reused as an application UI.
- Reusable board logic belongs in `components/bsp`; pages, state machines,
  animation, and application tasks belong in `main`.

## Project structure

```text
main/         application: protocol, state, storage, UI, network, audio, BLE
components/bsp/  board support package (borrowed from the upstream baseline)
assets/fonts/ generated Chinese bitmap font subsets
tests/        host tests that run without hardware
tools/        validation, firmware archiving, font generation
docs/         architecture notes
```

## Documentation conventions

This repository is Chinese-first. The primary documents are Simplified
Chinese, with English summaries alongside:

| Chinese (primary) | English summary |
| --- | --- |
| `README.md` | `README.en.md` |
| `HANDOFF.md` | `HANDOFF.en.md` |
| `使用手册.md` | (covered by `README.en.md`) |
| `docs/architecture.md` | (covered by `README.en.md`) |

There is no enforced `.zh_CN.md` pairing here — that is the upstream baseline's
convention, not this repository's. What *is* enforced, by
`tools/check_repo.py`:

- every relative Markdown link target exists;
- no credentials, private keys, or real family codes are committed;
- no build artifacts (`*.bin`, `*.elf`, `*.map`) are committed;
- every `main/ft_*.c` is registered in `main/CMakeLists.txt`;
- the generated fonts are registered exactly once, and never `#include`d from
  another source file.

## Chinese text and fonts

The interface is in Simplified Chinese, and LVGL's default Montserrat fonts
contain no Chinese glyphs. UTF-8 correctness and a successful build do not
prove that Chinese renders on screen.

- `main/ft_texts.c` is the single source of truth for UI copy. Every string
  shown to the user belongs there.
- `tools/gen_fonts.py` subsets the CJK source font down to exactly those
  characters and runs `lv_font_conv`.
- `tests/test_font_coverage.py` fails the gate when a character in the UI copy
  is missing from the generated fonts.

When you change UI text, change `main/ft_texts.c`, regenerate the fonts, and
re-run the coverage check. Never hide missing glyphs by disabling the
placeholder renderer.

## Safety and runtime rules

- Preserve existing user changes. Run `git status --short --branch` first;
  never overwrite or clean unrelated files.
- LVGL is not thread-safe. Any code outside the LVGL task must hold
  `bsp_lvgl_lock()` while touching LVGL objects.
- Button callbacks must not block. Audio, storage, and networking belong in
  worker tasks.
- The WebSocket component's callback runs in its own task and must only copy
  data into a queue.
- Keep testable state machines, protocols, timing, and layout calculations
  free of ESP-IDF and LVGL, and cover them with host tests.
- Never commit credentials, device secrets, private keys, personal data, or
  unsanitized logs. The family code and the Wi-Fi password must never appear
  in a log line.
- The family code, the server address, and the Wi-Fi credentials live in an
  encrypted NVS partition. Do not move them to plain storage.
- The server certificate is validated against the system certificate bundle.
  There is deliberately no "skip verification" default; adding one requires
  explicit user consent.

## Task-specific context routing

| Task | Read before editing |
| --- | --- |
| Any code change | `docs/architecture.md`, the affected headers and neighbouring implementation |
| Protocol or state machine | `main/ft_protocol.c`, `main/ft_state.c`, and their host tests |
| UI, screens, or text | `main/ft_ui.c`, `main/ft_texts.c`, `assets/fonts/README.md` |
| Networking or audio | `main/ft_net.c`, `main/ft_audio.c` |
| BLE provisioning | `main/ft_prov.c`, the upstream `demo/blufi-provisioning` reference |
| Build, test, dependencies, partitions | `tools/validate.sh`, `sdkconfig.defaults`, `partitions.csv` |
| Fonts | `assets/fonts/README.md`, `tools/gen_fonts.py` |
| Documentation | `README.md`, `HANDOFF.md`, and this file |

Consult the upstream hardware guide for board facts:
[AI_HARDWARE_DEVELOPMENT_GUIDE](https://gitee.com/FoloToy/ai-passport/blob/main/docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md).
If a task needs a board detail that is not defined there or in
`components/bsp/include/bsp_pins.h`, ask the user instead of guessing.

## Required validation and delivery

Run the smallest relevant check while iterating, then the complete gate before
delivery:

```bash
./tools/validate.sh --static    # repository checks + host tests
./tools/validate.sh --firmware  # ESP-IDF build + merged-image verification
./tools/validate.sh             # complete gate
```

The complete gate requires an activated ESP-IDF 5.5.3 environment. Do not
describe a successful build as hardware validation. Final delivery must report
these fields separately:

```text
Build: PASS / FAIL / NOT RUN
Host tests: PASS / FAIL / NOT RUN
Device tests: PASS / FAIL / NOT RUN
Unverified: remaining board, instrument, or user checks
```

After each complete implementation of a user-requested firmware change,
proactively ask whether to flash it to the device for testing; do not wait for
a release request. If no device is detected, ask the user to turn it on and
connect it to a computer USB port with a data-capable cable. Flash only after
explicit approval; detection alone is not consent.

Create commits and push only when the user requests them or the active
workflow explicitly requires them.
