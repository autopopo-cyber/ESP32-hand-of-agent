#!/usr/bin/env python3
"""ODA HID Bridge CLI — control keyboard & mouse via CDC serial."""

import struct
import argparse
import sys
import time
import serial

# Command opcodes (must match firmware cdc_protocol.h)
CMD_MOUSE_MOVE    = 0x01
CMD_MOUSE_MOVE_TO = 0x02
CMD_MOUSE_CLICK   = 0x03
CMD_MOUSE_PRESS   = 0x04
CMD_MOUSE_RELEASE = 0x05
CMD_MOUSE_SCROLL  = 0x06
CMD_KEY_PRESS     = 0x07
CMD_KEY_RELEASE   = 0x08
CMD_KEY_TAP       = 0x09
CMD_KEY_TYPE      = 0x0A
CMD_SYNC          = 0x0B
CMD_NOOP          = 0x0C
CMD_LED_SET       = 0xFD
CMD_ACK_REQUEST   = 0xFE
CMD_RESET         = 0xFF

BTN_LEFT   = 1
BTN_RIGHT  = 2
BTN_MIDDLE = 4

# HID keyboard codes for named keys
HID_KEYS = {
    'enter': 0x28, 'return': 0x28, 'esc': 0x29, 'escape': 0x29,
    'backspace': 0x2A, 'bksp': 0x2A,
    'tab': 0x2B, 'space': 0x2C,
    'minus': 0x2D, 'equal': 0x2E,
    'left': 0x50, 'right': 0x4F, 'up': 0x52, 'down': 0x51,
    'home': 0x4A, 'end': 0x4D, 'pgup': 0x4B, 'pgdn': 0x4E,
    'insert': 0x49, 'delete': 0x4C,
    'capslock': 0x39,
    'ctrl': 0xE0, 'lctrl': 0xE0, 'rctrl': 0xE4,
    'shift': 0xE1, 'lshift': 0xE1, 'rshift': 0xE5,
    'alt': 0xE2, 'lalt': 0xE2, 'ralt': 0xE6,
    'win': 0xE3, 'lwin': 0xE3, 'rwin': 0xE7,
    'f1': 0x3A, 'f2': 0x3B, 'f3': 0x3C, 'f4': 0x3D,
    'f5': 0x3E, 'f6': 0x3F, 'f7': 0x40, 'f8': 0x41,
    'f9': 0x42, 'f10': 0x43, 'f11': 0x44, 'f12': 0x45,
    'a': 0x04, 'b': 0x05, 'c': 0x06, 'd': 0x07,
    'e': 0x08, 'f': 0x09, 'g': 0x0A, 'h': 0x0B,
    'i': 0x0C, 'j': 0x0D, 'k': 0x0E, 'l': 0x0F,
    'm': 0x10, 'n': 0x11, 'o': 0x12, 'p': 0x13,
    'q': 0x14, 'r': 0x15, 's': 0x16, 't': 0x17,
    'u': 0x18, 'v': 0x19, 'w': 0x1A, 'x': 0x1B,
    'y': 0x1C, 'z': 0x1D,
    '0': 0x27, '1': 0x1E, '2': 0x1F, '3': 0x20, '4': 0x21,
    '5': 0x22, '6': 0x23, '7': 0x24, '8': 0x25, '9': 0x26,
}

BUTTON_MAP = {'left': BTN_LEFT, 'right': BTN_RIGHT, 'middle': BTN_MIDDLE}


class OdaBridge:
    def __init__(self, port, baud=115200):
        self.ser = serial.Serial(port, baud, timeout=1)

    def close(self):
        self.ser.close()

    def _send(self, data):
        self.ser.write(data)
        self.ser.flush()

    def mouse_move(self, dx, dy):
        self._send(struct.pack('<Bhh', CMD_MOUSE_MOVE, dx, dy))

    def mouse_move_to(self, x, y):
        self._send(struct.pack('<BHH', CMD_MOUSE_MOVE_TO, x, y))

    def mouse_click(self, button=BTN_LEFT):
        self._send(struct.pack('<BB', CMD_MOUSE_CLICK, button))

    def mouse_press(self, button=BTN_LEFT):
        self._send(struct.pack('<BB', CMD_MOUSE_PRESS, button))

    def mouse_release(self, button=BTN_LEFT):
        self._send(struct.pack('<BB', CMD_MOUSE_RELEASE, button))

    def mouse_scroll(self, delta):
        self._send(struct.pack('<Bb', CMD_MOUSE_SCROLL, delta))

    def key_press(self, hid_code):
        self._send(struct.pack('<BB', CMD_KEY_PRESS, hid_code))

    def key_release(self, hid_code):
        self._send(struct.pack('<BB', CMD_KEY_RELEASE, hid_code))

    def key_tap(self, hid_code, count=1):
        self._send(struct.pack('<BBB', CMD_KEY_TAP, hid_code, count))

    def key_type(self, text):
        data = text.encode('ascii', errors='ignore')
        if len(data) > 127:
            data = data[:127]
        if len(data) == 0:
            return
        self._send(struct.pack('<BB', CMD_KEY_TYPE, len(data)) + data)

    def noop(self, delay_ms):
        self._send(struct.pack('<BH', CMD_NOOP, min(delay_ms, 10000)))

    def led_set(self, r, g, b):
        self._send(struct.pack('<BBBB', CMD_LED_SET, r, g, b))

    def reset(self):
        self._send(bytes([CMD_RESET]))


def _get_code(key_name):
    code = HID_KEYS.get(key_name.lower())
    if code is None:
        print(f"Unknown key: {key_name}", file=sys.stderr)
        print(f"Known keys: {' '.join(sorted(HID_KEYS))}", file=sys.stderr)
        sys.exit(1)
    return code


def main():
    parser = argparse.ArgumentParser(
        description='ODA HID Bridge — keyboard & mouse control via CDC serial')
    parser.add_argument('-p', '--port', default='COM9',
                        help='Serial port (default: COM9)')

    sub = parser.add_subparsers(dest='cmd', help='Commands')

    # Mouse
    p = sub.add_parser('move', help='Mouse move (relative)')
    p.add_argument('dx', type=int, help='Delta X (pixels)')
    p.add_argument('dy', type=int, help='Delta Y (pixels)')

    p = sub.add_parser('click', help='Mouse click')
    p.add_argument('-b', '--button', choices=['left', 'right', 'middle'],
                   default='left')

    p = sub.add_parser('press', help='Mouse press (hold)')
    p.add_argument('-b', '--button', choices=['left', 'right', 'middle'],
                   default='left')

    p = sub.add_parser('release', help='Mouse release')
    p.add_argument('-b', '--button', choices=['left', 'right', 'middle'],
                   default='left')

    p = sub.add_parser('scroll', help='Mouse scroll')
    p.add_argument('delta', type=int, help='Positive=up, negative=down')

    # Keyboard
    p = sub.add_parser('tap', help='Key tap (press and release)')
    p.add_argument('key', help='Key name (e.g. enter, a, f1, space)')
    p.add_argument('-n', '--count', type=int, default=1)

    p = sub.add_parser('type', help='Type text string')
    p.add_argument('text', nargs='+', help='Text to type')

    p = sub.add_parser('kpress', help='Key press (hold down)')
    p.add_argument('key', help='Key name')

    p = sub.add_parser('krelease', help='Key release')
    p.add_argument('key', help='Key name')

    # Macros
    p = sub.add_parser('combo', help='Press key combo (e.g. ctrl+c)')
    p.add_argument('keys', nargs='+', help='Keys to press together')

    # Device
    sub.add_parser('reset', help='Soft reset device')
    sub.add_parser('led', help='Set LED color').add_argument(
        'color', help='Hex RGB (e.g. FF0000)')

    args = parser.parse_args()

    if not args.cmd:
        parser.print_help()
        return

    bridge = OdaBridge(args.port)

    try:
        if args.cmd == 'move':
            bridge.mouse_move(args.dx, args.dy)
        elif args.cmd == 'click':
            bridge.mouse_click(BUTTON_MAP[args.button])
        elif args.cmd == 'press':
            bridge.mouse_press(BUTTON_MAP[args.button])
        elif args.cmd == 'release':
            bridge.mouse_release(BUTTON_MAP[args.button])
        elif args.cmd == 'scroll':
            bridge.mouse_scroll(args.delta)
        elif args.cmd == 'tap':
            bridge.key_tap(_get_code(args.key), args.count)
        elif args.cmd == 'type':
            bridge.key_type(' '.join(args.text))
        elif args.cmd == 'kpress':
            bridge.key_press(_get_code(args.key))
        elif args.cmd == 'krelease':
            bridge.key_release(_get_code(args.key))
        elif args.cmd == 'combo':
            codes = [_get_code(k) for k in args.keys]
            for c in codes:
                bridge.key_press(c)
            time.sleep(0.05)
            for c in reversed(codes):
                bridge.key_release(c)
        elif args.cmd == 'reset':
            bridge.reset()
        elif args.cmd == 'led':
            rgb = bytes.fromhex(args.color)
            bridge.led_set(rgb[0], rgb[1], rgb[2])

        # Small delay to let command process
        time.sleep(0.01)
        print(f"[ok] {args.cmd}")

    finally:
        bridge.close()


if __name__ == '__main__':
    main()
