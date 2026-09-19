# AGENTS.md

Guidance for AI coding agents working in this repository.

## Project overview

Firmware (Keil MDK, DA14585, SDK 6.0.22.1401) + Web Bluetooth control page for
a Hema ESL e-paper clock. See `Readme.MD` and `Programming.md` for hardware,
flash layout, and programming details.

## Single source of truth (important)

The web app exists in two hand-maintained copies plus one generated copy:

| File | Role |
|------|------|
| `WebApp/WebApp_EN/index.html` | **Source of truth.** Make all changes here. |
| `WebApp/WebApp_CN/webApp.html` | Manual Chinese translation. Apply every EN change here too (translated strings only; logic must stay identical). |
| `docs/index.html` | **Generated — do not edit by hand.** Copied from WebApp_EN by `.github/workflows/sync-docs.yml` on every push that touches it; served by GitHub Pages at `https://terenceang.github.io/HMCLOCK-EN/`. |

When changing web-app logic, always update both `WebApp_EN` and `WebApp_CN` in
the same commit so the translations never drift.

## BLE command protocol (custom service 0xFF00, characteristic 0xFF01)

| Command | Payload | Meaning |
|---------|---------|---------|
| `0x91` | 9 bytes: year(lo,hi), month(0-11), mday(1-31), hour, minute, second, wday | Set clock |
| `0x92` | 3 bytes: diff_sec (int16 LE), 0 | Time calibration offset |
| `>=0xA0` | see `ota_handle` | OTA update |
| `0x90` | — | **Removed** (0xA50f0010): toggled an `h24_format` flag nothing ever read; do not reintroduce |

## Firmware version bump convention

Any firmware change bumps `EPD_VERSION` in `src/config/user_config.h`
(format `0xA50F00xx`) and adds a changelog entry to **both** `Readme.MD` and
`README_cn.md`.

## CLI compile check (no Keil GUI needed)

Syntax-check a firmware source file with the exact project toolchain
(armclang V6.24) and the project's real include paths. Run from `Keil_5/`:

```powershell
# 1. Include paths: extract from .uvprojx (line with the first non-empty <IncludePath>)
$line = (Get-Content 'ble_app_peripheral.uvprojx')[342]
$inc = $line.Trim().Replace('<IncludePath>','').Replace('</IncludePath>','')
$args = ($inc -split ';' | Where-Object { $_ -and (Test-Path $_) } | ForEach-Object { "-I$_" })
$args += "-I..\src\config","-I..\src","-I..\src\epd"

# 2. Syntax-only compile. The -include flags replicate Keil's MiscControls
#    (project pulls its config through forced includes, NOT command-line defines).
& "C:\Users\teren\AppData\Local\Keil_v5\ARM\ARMCLANG\Bin\armclang.exe" `
  --target=arm-arm-none-eabi -mcpu=cortex-m0 -mthumb -xc -std=c99 -fsyntax-only `
  -include da1458x_config_basic.h -include da1458x_config_advanced.h -include user_config.h `
  @args "..\src\user_custs1_impl.c"
```

`exit: 0` = clean. Gotchas learned the hard way:

* The project passes **no `-D` defines** — all config comes from the three
  `-include` flags. Forgetting them produces a wall of bogus "undeclared
  identifier BLE_NB_PROFILES"-style errors from SDK headers.
* Do **not** reorder definitions in `user_custs1_impl.c`: `epd_commit()`
  references `epd_wait_timer`/`epd_wait_hnd`, so those helpers must stay
  *below* `epd_wait_timer`'s definition.
* For web-app changes, extract each `<script>` block to a temp `.js` file and
  run `node --check` on it.
