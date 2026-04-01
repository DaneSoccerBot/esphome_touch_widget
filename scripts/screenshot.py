#!/usr/bin/env python3
"""ESP32 Screenshot & Telemetry Tool — Protokoll v2 mit CRC32.

Usage:
  python scripts/screenshot.py                              # einmaliger Screenshot
  python scripts/screenshot.py --watch                      # kontinuierlich, Enter pro Screenshot
  python scripts/screenshot.py --info                       # System-Info abrufen (Heap/PSRAM)
  python scripts/screenshot.py --benchmark                  # Render-Benchmark ausführen
  python scripts/screenshot.py -o grid_view.png             # eigener Dateiname
  python scripts/screenshot.py --port /dev/cu.usbserial-X   # anderer Port
"""

import argparse
import struct
import sys
import time
import zlib
from pathlib import Path

import serial
from PIL import Image

MARKER_SCR_V1 = b"\x89SCR\x01"
MARKER_SCR_V2 = b"\x89SCR\x02"
MARKER_TXT = b"\x89TXT\x01"
MARKER_END = b"\x89END"
DEFAULT_PORT = "/dev/cu.usbserial-21230"
DEFAULT_BAUD = 115200


def rgb565_to_rgb888(pixel: int) -> tuple[int, int, int]:
    r = ((pixel >> 11) & 0x1F) << 3
    g = ((pixel >> 5) & 0x3F) << 2
    b = (pixel & 0x1F) << 3
    return (r, g, b)


def _find_marker(ser: serial.Serial, timeout: float = 15.0) -> str | None:
    """Liest vom Serial-Port bis ein bekannter Marker gefunden wird.

    Returns: 'scr_v1', 'scr_v2', 'txt', oder None bei Timeout.
    """
    buf = b""
    deadline = time.time() + timeout
    markers = {
        MARKER_SCR_V1: "scr_v1",
        MARKER_SCR_V2: "scr_v2",
        MARKER_TXT: "txt",
    }
    max_marker_len = max(len(m) for m in markers)

    while time.time() < deadline:
        byte = ser.read(1)
        if not byte:
            continue
        buf += byte

        # Log-Zeilen im Durchlauf anzeigen
        if byte == b"\n":
            try:
                line = buf[:-1].decode("utf-8", errors="replace").strip()
                if line and b"\x89" not in buf:
                    print(f"  [LOG] {line}", flush=True)
            except Exception:
                pass
            buf = b""
            continue

        # Marker prüfen
        for marker, name in markers.items():
            if buf.endswith(marker):
                return name

        # Buffer begrenzen
        if len(buf) > 256:
            buf = buf[-max_marker_len:]

    return None


def _receive_text(ser: serial.Serial, timeout: float = 10.0) -> str | None:
    """Empfängt Text-Response bis END-Marker."""
    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        chunk = ser.read(max(1, ser.in_waiting or 1))
        if not chunk:
            continue
        buf += chunk
        end_pos = buf.find(MARKER_END)
        if end_pos >= 0:
            return buf[:end_pos].decode("utf-8", errors="replace")
    print("  FEHLER: Text-Response Timeout", file=sys.stderr)
    return None


def receive_screenshot(ser: serial.Serial, version: str = "scr_v2") -> Image.Image | None:
    """Empfängt einen Screenshot nach gefundenem START-Marker."""
    has_crc = version == "scr_v2"
    header_size = 12 if has_crc else 8

    header = ser.read(header_size)
    if len(header) < header_size:
        print(f"  FEHLER: Header unvollständig ({len(header)}/{header_size} Bytes)", file=sys.stderr)
        return None

    if has_crc:
        width, height, payload_size, expected_crc = struct.unpack("<HHII", header)
    else:
        width, height, payload_size = struct.unpack("<HHI", header)
        expected_crc = None

    rle_pairs = payload_size // 4
    raw_size = width * height * 2
    compression = raw_size / payload_size if payload_size > 0 else 0
    print(f"  {width}×{height}, {rle_pairs} RLE-Paare, {payload_size} B ({compression:.1f}x Kompression)")
    if expected_crc is not None:
        print(f"  CRC32: 0x{expected_crc:08X}")

    # Plausibilitätsprüfung
    if width == 0 or height == 0 or width > 4096 or height > 4096:
        print(f"  FEHLER: Ungültige Dimensionen {width}×{height}", file=sys.stderr)
        return None
    if payload_size > raw_size * 2:
        print(f"  FEHLER: Payload ({payload_size}) größer als 2× Rohgröße ({raw_size})", file=sys.stderr)
        return None

    # Payload empfangen
    t0 = time.time()
    data = b""
    last_progress = 0
    while len(data) < payload_size:
        remaining = payload_size - len(data)
        chunk = ser.read(min(4096, remaining))
        if not chunk:
            print(f"\n  FEHLER: Timeout ({len(data)}/{payload_size} Bytes)", file=sys.stderr)
            return None
        data += chunk

        pct = len(data) * 100 // payload_size
        if pct >= last_progress + 10:
            print(f"  {pct}%...", end="", flush=True)
            last_progress = pct

    rx_time = time.time() - t0
    effective_baud = len(data) * 10 / rx_time if rx_time > 0 else 0
    print(f"  100% ({rx_time:.1f}s, ~{effective_baud:.0f} Baud effektiv)", flush=True)

    # END-Marker prüfen
    footer = ser.read(4)
    if footer != MARKER_END:
        print(f"  WARNUNG: ungültiger Footer: {footer.hex()}", file=sys.stderr)

    # CRC32 verifizieren
    if expected_crc is not None:
        actual_crc = zlib.crc32(data) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            print(f"  FEHLER: CRC mismatch! Erwartet 0x{expected_crc:08X}, "
                  f"berechnet 0x{actual_crc:08X}", file=sys.stderr)
            return None
        print(f"  CRC32 OK ✓")

    # RLE dekodieren
    pixels: list[int] = []
    for offset in range(0, len(data), 4):
        count, color = struct.unpack_from("<HH", data, offset)
        pixels.extend([color] * count)

    expected_px = width * height
    if len(pixels) != expected_px:
        print(f"  WARNUNG: erwartet {expected_px} Pixel, erhalten {len(pixels)}", file=sys.stderr)

    # RGB565 → RGB888 → PIL Image
    img = Image.new("RGB", (width, height))
    img_data = [rgb565_to_rgb888(p) for p in pixels[:expected_px]]
    img.putdata(img_data)
    return img


def _open_serial(port: str, baud: int) -> serial.Serial:
    """Öffnet Serial-Port mit stabilen Einstellungen."""
    ser = serial.Serial(port, baud, timeout=2)
    ser.dtr = False
    ser.rts = False
    time.sleep(0.1)
    return ser


def send_command(port: str, baud: int, cmd: str, timeout: float = 15.0) -> str | None:
    """Sendet einen Befehl und empfängt die Text-Antwort."""
    print(f"Verbinde mit {port} @ {baud}...")
    ser = _open_serial(port, baud)

    print(f"Sende '{cmd}'...")
    ser.write(cmd.encode("ascii"))
    ser.flush()

    marker = _find_marker(ser, timeout)
    if marker is None:
        print("FEHLER: Keine Antwort (Timeout)", file=sys.stderr)
        ser.close()
        return None

    if marker == "txt":
        text = _receive_text(ser)
        ser.close()
        if text:
            print(text)
        return text

    print(f"  Unerwarteter Marker: {marker}", file=sys.stderr)
    ser.close()
    return None


def capture_one(port: str, baud: int, output: str, timeout: float = 15.0) -> bool:
    """Sendet Trigger und empfängt einen Screenshot."""
    print(f"Verbinde mit {port} @ {baud}...")
    ser = _open_serial(port, baud)

    print("Trigger 'S' gesendet, warte auf Screenshot...")
    ser.write(b"S")
    ser.flush()

    marker = _find_marker(ser, timeout)
    if marker is None:
        print("FEHLER: Kein Screenshot empfangen (Timeout)", file=sys.stderr)
        ser.close()
        return False

    if marker in ("scr_v1", "scr_v2"):
        img = receive_screenshot(ser, marker)
    else:
        print(f"  Unerwarteter Marker: {marker}", file=sys.stderr)
        ser.close()
        return False

    ser.close()

    if img is None:
        return False

    img.save(output)
    print(f"Gespeichert: {output}")
    return True


def watch_mode(port: str, baud: int, output_dir: str) -> None:
    """Kontinuierlicher Modus — Enter pro Screenshot."""
    out_path = Path(output_dir)
    out_path.mkdir(parents=True, exist_ok=True)

    print(f"Watch-Modus: Verbinde mit {port} @ {baud}...")
    ser = _open_serial(port, baud)

    count = 0
    try:
        while True:
            cmd = input("[S]creenshot [I]nfo [B]ench [F]ullscreen [P]age [R]un [Q]uit: ").strip().upper()
            if cmd == "Q":
                break
            if cmd not in ("S", "I", "B", "F", "P", "R"):
                cmd = "S"

            ser.write(cmd.encode("ascii"))
            ser.flush()

            # F/P senden nur Text-Antwort, R sendet mehrere Screenshots
            if cmd == "R":
                # Automated run: erwarte bis zu 3 Screenshots
                for shot in range(3):
                    marker = _find_marker(ser, timeout=30.0)
                    if not marker:
                        print(f"  Shot {shot+1}/3: Timeout")
                        break
                    if marker in ("scr_v1", "scr_v2"):
                        img = receive_screenshot(ser, marker)
                        if img:
                            count += 1
                            ts = time.strftime("%H%M%S")
                            fname = out_path / f"screenshot_{ts}_{count:03d}.png"
                            img.save(str(fname))
                            print(f"  Shot {shot+1}/3: {fname}")
                    elif marker == "txt":
                        text = _receive_text(ser)
                        if text:
                            print(text)
                continue

            if cmd in ("F", "P"):
                # Scene commands: optional text response, then trigger update
                marker = _find_marker(ser, timeout=5.0)
                if marker == "txt":
                    text = _receive_text(ser)
                    if text:
                        print(text)
                elif marker:
                    print(f"OK (marker={marker})")
                else:
                    print("OK (kein Marker)")
                continue

            marker = _find_marker(ser, timeout=15.0)
            if marker is None:
                print("Timeout — keine Antwort")
                continue

            if marker in ("scr_v1", "scr_v2"):
                img = receive_screenshot(ser, marker)
                if img:
                    count += 1
                    ts = time.strftime("%H%M%S")
                    fname = out_path / f"screenshot_{ts}_{count:03d}.png"
                    img.save(str(fname))
                    print(f"Gespeichert: {fname}")
            elif marker == "txt":
                text = _receive_text(ser)
                if text:
                    print(text)
    except KeyboardInterrupt:
        print(f"\nBeendet. {count} Screenshots gespeichert.")
    finally:
        ser.close()


def main():
    parser = argparse.ArgumentParser(description="ESP32 Screenshot & Telemetry Tool (Protokoll v2)")
    parser.add_argument("--port", default=DEFAULT_PORT, help=f"Serial port (default: {DEFAULT_PORT})")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"Baud rate (default: {DEFAULT_BAUD})")
    parser.add_argument("-o", "--output", default="screenshot.png", help="Output file (default: screenshot.png)")
    parser.add_argument("--watch", action="store_true", help="Interaktiver Modus (S/I/B/F/P/R/Q)")
    parser.add_argument("--watch-dir", default="screenshots", help="Ausgabe-Ordner für Watch-Modus")
    parser.add_argument("--info", action="store_true", help="System-Info abrufen (Heap, PSRAM)")
    parser.add_argument("--benchmark", action="store_true", help="Render-Benchmark ausführen")
    args = parser.parse_args()

    if args.info:
        send_command(args.port, args.baud, "I")
    elif args.benchmark:
        send_command(args.port, args.baud, "B")
    elif args.watch:
        watch_mode(args.port, args.baud, args.watch_dir)
    else:
        ok = capture_one(args.port, args.baud, args.output)
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
