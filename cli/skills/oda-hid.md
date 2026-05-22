---
name: oda-hid
description: Control keyboard and mouse via ESP32S3 ODA HID Bridge over USB CDC serial
type: flexible
---

# ODA HID Bridge Control

Control the PC's keyboard and mouse through the ESP32S3 ODA HID Bridge.

## Usage

```
python cli/oda_cli.py -p COM9 <command> [args...]
```

## Commands

### Mouse

| Command | Description | Example |
|---------|-------------|---------|
| `move DX DY` | Relative mouse move (pixels) | `python cli/oda_cli.py move 100 -50` |
| `click [-b left|right|middle]` | Mouse click | `python cli/oda_cli.py click -b right` |
| `press [-b left|right|middle]` | Mouse press (hold) | `python cli/oda_cli.py press` |
| `release [-b left|right|middle]` | Mouse release | `python cli/oda_cli.py release` |
| `scroll DELTA` | Scroll (positive=up) | `python cli/oda_cli.py scroll 3` |

### Keyboard

| Command | Description | Example |
|---------|-------------|---------|
| `tap KEY [-n COUNT]` | Key tap | `python cli/oda_cli.py tap enter` |
| `type TEXT...` | Type text string | `python cli/oda_cli.py type hello world` |
| `kpress KEY` | Key press (hold) | `python cli/oda_cli.py kpress shift` |
| `krelease KEY` | Key release | `python cli/oda_cli.py krelease shift` |
| `combo KEY1 KEY2...` | Key combination | `python cli/oda_cli.py combo ctrl c` |

### Device

| Command | Description |
|---------|-------------|
| `reset` | Soft reset ESP32S3 |
| `led HEXRGB` | Set LCD backlight color |

## Available Key Names

Modifiers: `ctrl`, `lctrl`, `rctrl`, `shift`, `lshift`, `rshift`, `alt`, `lalt`, `ralt`, `win`

Navigation: `enter`, `esc`, `tab`, `space`, `backspace`, `left`, `right`, `up`, `down`, `home`, `end`, `pgup`, `pgdn`, `insert`, `delete`

Function: `f1`-`f12`, `capslock`

Letters: `a`-`z`

Numbers: `0`-`9`

Symbols: use `type` command instead for these

## Default Port

The CLI defaults to COM9. Use `-p` to change: `python cli/oda_cli.py -p COM3 type hello`
