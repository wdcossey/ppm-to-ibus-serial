#!/usr/bin/env python3
"""Generates the wiring diagrams (SVG) in this folder.

The boards are simplified drawings: the header pins are in roughly the right
places, but always check the labels printed on your own board.

Usage: python3 make_diagrams.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PITCH = 22                      # Distance between header pins
WIRE = {"sig": "#f28c28", "vcc": "#d62728", "gnd": "#222222"}
WIRE_NAME = {"sig": "PPM signal", "vcc": "+5V", "gnd": "GND"}

# A header row: (x, y, labels, label position "above" / "below" / "left" / "right", direction "h" / "v")
BOARDS = {
    "uno": dict(
        title="Arduino Uno", size=(530, 290), usb="left",
        headers=[
            (110, 20, ["SCL", "SDA", "AREF", "GND", "13", "12", "11", "10", "9", "8"], "below", "h"),
            (110 + 10 * PITCH + 10, 20, ["7", "6", "5", "4", "3", "2", "1", "0"], "below", "h"),
            (170, 270, ["", "IORF", "RST", "3V3", "5V", "GND", "GND", "VIN"], "above", "h"),
            (170 + 8 * PITCH + 10, 270, ["A0", "A1", "A2", "A3", "A4", "A5"], "above", "h"),
        ],
        led=(165, 70, "LED 13"),
    ),
    "nano": dict(
        title="Arduino Nano", size=(380, 130), usb="left",
        headers=[
            (40, 18, ["D12", "D11", "D10", "D9", "D8", "D7", "D6", "D5", "D4", "D3", "D2", "GND", "RST", "RX0", "TX1"], "below", "h"),
            (40, 112, ["D13", "3V3", "REF", "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "5V", "RST", "GND", "VIN"], "above", "h"),
        ],
        led=(70, 65, "LED 13"),
    ),
    "promini": dict(
        title="Arduino Pro Mini (5V / 16MHz)", size=(350, 150), usb=None,
        headers=[
            (80, 18, ["TXO", "RXI", "RST", "GND", "2", "3", "4", "5", "6", "7", "8", "9"], "below", "h"),
            (80, 132, ["RAW", "GND", "RST", "VCC", "A3", "A2", "A1", "A0", "13", "12", "11", "10"], "above", "h"),
            (18, 32, ["GRN", "TXO", "RXI", "VCC", "GND", "BLK"], "right", "v"),
        ],
        led=(170, 75, "LED 13"),
    ),
    "leonardo": dict(
        title="Arduino Leonardo", size=(530, 290), usb="left",
        headers=[
            (110, 20, ["SCL", "SDA", "AREF", "GND", "13", "12", "11", "10", "9", "8"], "below", "h"),
            (110 + 10 * PITCH + 10, 20, ["7", "6", "5", "4", "3", "2", "1", "0"], "below", "h"),
            (170, 270, ["", "IORF", "RST", "3V3", "5V", "GND", "GND", "VIN"], "above", "h"),
            (170 + 8 * PITCH + 10, 270, ["A0", "A1", "A2", "A3", "A4", "A5"], "above", "h"),
        ],
        led=(165, 70, "LED 13"),
    ),
    "promicro": dict(
        title="Pro Micro (5V / 16MHz)", size=(330, 140), usb="left",
        headers=[
            (60, 18, ["TX0", "RX1", "GND", "GND", "2", "3", "4", "5", "6", "7", "8", "9"], "below", "h"),
            (60, 122, ["RAW", "GND", "RST", "VCC", "A3", "A2", "A1", "A0", "15", "14", "16", "10"], "above", "h"),
        ],
        led=(150, 70, "RX LED (pin 17)"),
    ),
    "mega": dict(
        title="Arduino Mega 2560", size=(680, 320), usb="left",
        headers=[
            (110, 20, ["SCL", "SDA", "AREF", "GND", "13", "12", "11", "10", "9", "8"], "below", "h"),
            (110 + 10 * PITCH + 10, 20, ["7", "6", "5", "4", "3", "2", "1", "0"], "below", "h"),
            (170, 300, ["", "IORF", "RST", "3V3", "5V", "GND", "GND", "VIN"], "above", "h"),
            (620, 14, ["5V"] + [str(n) for n in range(22, 53, 2)] + ["GND"], "left", "v"),
            (642, 14, ["5V"] + [str(n) for n in range(23, 54, 2)] + ["GND"], "right", "v"),
        ],
        led=(165, 70, "LED 13"),
    ),
}

# Which header pins to use: board -> {wire: (header index, pin label)}
PPM_WIRING = {
    "uno": {"sig": (0, "8"), "vcc": (2, "5V"), "gnd": (2, "GND")},
    "nano": {"sig": (0, "D8"), "vcc": (1, "5V"), "gnd": (1, "GND")},
    "promini": {"sig": (0, "8"), "vcc": (1, "VCC"), "gnd": (1, "GND")},
    "leonardo": {"sig": (1, "4"), "vcc": (2, "5V"), "gnd": (2, "GND")},
    "promicro": {"sig": (0, "4"), "vcc": (1, "VCC"), "gnd": (1, "GND")},
    "mega": {"sig": (4, "49"), "vcc": (4, "5V"), "gnd": (4, "GND")},
}

FONT = "font-family='Helvetica, Arial, sans-serif'"


def esc(text):
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def pin_positions(header, ox, oy):
    x, y, labels, _, direction = header
    for i, label in enumerate(labels):
        if direction == "h":
            yield label, (ox + x + i * PITCH, oy + y)
        else:
            yield label, (ox + x, oy + y + i * PITCH * 0.75)


def find_pin(board, header_index, label, ox, oy):
    for l, pos in pin_positions(board["headers"][header_index], ox, oy):
        if l == label:
            return pos
    raise KeyError(label)


def draw(name, wiring, receiver_title, notes):
    board = BOARDS[name]
    bw, bh = board["size"]
    ox, oy = 100, 70                                  # Board position
    rx, ry, rw, rh = ox + bw + 140, oy + bh / 2 - 60, 150, 120   # Receiver box
    width, height = rx + rw + 30, oy + bh + 60 + 22 * len(notes)

    out = [f"<svg xmlns='http://www.w3.org/2000/svg' width='{width:.0f}' height='{height:.0f}' viewBox='0 0 {width:.0f} {height:.0f}' {FONT}>",
           f"<rect width='100%' height='100%' fill='#ffffff'/>",
           f"<text x='30' y='34' font-size='20' font-weight='bold' fill='#222'>{esc(board['title'])}</text>"]

    # Board
    out.append(f"<rect x='{ox}' y='{oy}' width='{bw}' height='{bh}' rx='10' fill='#2f4f5f' stroke='#1d3340' stroke-width='2'/>")
    if board["usb"] == "left":
        out.append(f"<rect x='{ox - 18}' y='{oy + bh / 2 - 22}' width='34' height='44' rx='3' fill='#c0c0c0' stroke='#888'/>")
        out.append(f"<text x='{ox - 24}' y='{oy + bh / 2 + 4}' font-size='12' fill='#444' text-anchor='end'>USB to PC</text>")
    if board.get("led"):
        lx, ly, ltext = board["led"]
        out.append(f"<circle cx='{ox + lx}' cy='{oy + ly}' r='6' fill='#ffd84d' stroke='#c9a500'/>")
        out.append(f"<text x='{ox + lx + 10}' y='{oy + ly + 4}' font-size='11' fill='#ffffff'>{esc(ltext)} (status)</text>")

    used = {}
    for wire, (hi, label) in wiring.items():
        used[(hi, label)] = wire

    order = ["sig", "vcc", "gnd"]
    rpins = {wire: (rx, ry + 30 + i * 30) for i, wire in enumerate(order)}

    # Wires (drawn first, the pins and labels go on top): out of the board edge, then across to the receiver
    for n, (wire, (hi, label)) in enumerate(sorted(wiring.items(), key=lambda w: order.index(w[0]))):
        px, py = find_pin(board, hi, label, ox, oy)
        tx, ty = rpins[wire]
        lane_x = ox + bw + 30 + n * 28
        side = board["headers"][hi][3]
        if side in ("below", "above"):
            edge_y = oy - 18 - n * 12 if side == "below" else oy + bh + 18 + n * 12
            points = [(px, py), (px, edge_y), (lane_x, edge_y), (lane_x, ty), (tx, ty)]
        else:
            points = [(px, py), (lane_x, py), (lane_x, ty), (tx, ty)]
        path = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
        out.append(f"<polyline points='{path}' fill='none' stroke='{WIRE[wire]}' stroke-width='3.5' stroke-linejoin='round' stroke-linecap='round'/>")
        out.append(f"<circle cx='{px}' cy='{py}' r='4' fill='{WIRE[wire]}'/>")

    # Headers
    highlighted = set()
    for hi, header in enumerate(board["headers"]):
        side = header[3]
        for label, (px, py) in pin_positions(header, ox, oy):
            if not label:
                continue
            wire = used.get((hi, label)) if (hi, label) not in highlighted else None
            if wire:
                highlighted.add((hi, label))           # Only the first pin with that name is wired
            fill = WIRE[wire] if wire else "#111111"
            out.append(f"<rect x='{px - 5}' y='{py - 5}' width='10' height='10' fill='{fill}' stroke='#cccccc' stroke-width='1'/>")
            halo = "stroke='#2f4f5f' stroke-width='3' paint-order='stroke'"
            weight = "bold" if wire else "normal"
            colour = "#ffffff" if not wire else "#ffe9a8"
            size = 10 if not wire else 11
            if side == "below":
                out.append(f"<text x='{px}' y='{py + 10}' font-size='{size}' font-weight='{weight}' fill='{colour}' {halo} text-anchor='end' transform='rotate(-90 {px} {py + 10})' dy='3'>{esc(label)}</text>")
            elif side == "above":
                out.append(f"<text x='{px}' y='{py - 10}' font-size='{size}' font-weight='{weight}' fill='{colour}' {halo} text-anchor='start' transform='rotate(-90 {px} {py - 10})' dy='3'>{esc(label)}</text>")
            elif side == "left":
                out.append(f"<text x='{px - 9}' y='{py + 3}' font-size='{size - 1}' font-weight='{weight}' fill='{colour}' {halo} text-anchor='end'>{esc(label)}</text>")
            else:
                out.append(f"<text x='{px + 9}' y='{py + 3}' font-size='{size - 1}' font-weight='{weight}' fill='{colour}' {halo} text-anchor='start'>{esc(label)}</text>")

    # Receiver
    out.append(f"<rect x='{rx}' y='{ry}' width='{rw}' height='{rh}' rx='8' fill='#f4f4f4' stroke='#555' stroke-width='2'/>")
    out.append(f"<text x='{rx + rw / 2}' y='{ry + rh + 20}' font-size='13' font-weight='bold' fill='#222' text-anchor='middle'>{esc(receiver_title)}</text>")
    for wire in order:
        py = rpins[wire][1]
        out.append(f"<rect x='{rx - 5}' y='{py - 5}' width='10' height='10' fill='{WIRE[wire]}' stroke='#333'/>")
        out.append(f"<text x='{rx + 12}' y='{py + 4}' font-size='12' fill='#222'>{esc(WIRE_NAME[wire])}</text>")

    # Notes
    for i, note in enumerate(notes):
        out.append(f"<text x='30' y='{oy + bh + 60 + i * 22}' font-size='13' fill='#333'>{esc(note)}</text>")

    out.append("</svg>")
    return "\n".join(out) + "\n"


PPM_NOTES = {
    "uno": ["PPM signal to D8. Also works the same on the Nano and Pro Mini (D8)."],
    "nano": ["PPM signal to D8."],
    "promini": ["PPM signal to pin 8. On a 3.3V / 8MHz Pro Mini, power the receiver from 5V", "(into RAW) and check that its PPM signal is no higher than 3.3V."],
    "leonardo": ["PPM signal to D4. The Arduino Micro uses D4 as well."],
    "promicro": ["PPM signal to pin 4. For the status LED set STATUS_LED_PIN 17 and STATUS_LED_ON LOW."],
    "mega": ["PPM signal to D49 (double row header). 5V and GND are at the ends of the same header."],
}

if __name__ == "__main__":
    for name, wiring in PPM_WIRING.items():
        path = os.path.join(HERE, f"ppm-{name}.svg")
        with open(path, "w") as f:
            f.write(draw(name, wiring, "PPM receiver", PPM_NOTES[name]))
        print("wrote", path)
