#!/usr/bin/env python3
"""
Automated bed flatness measurement script for Prusa Buddy firmware.

Connects to the printer over serial, homes, heats the nozzle and bed,
then repeatedly runs M1963 A (bed flatness grid probe in auto mode)
for the configured duration, saving all measurement reports to a
timestamped file on disk.

Requires the M1963 'A' (auto) parameter added in this fork — the
printer must be running firmware that supports ``M1963 A``.

Usage
-----
    python3 measure_bed_flatness.py /dev/ttyACM0
    python3 measure_bed_flatness.py /dev/ttyACM0 --duration 30 --nozzle 200 --bed 110
    python3 measure_bed_flatness.py COM3 --output results.txt    # Windows

Requirements
------------
    pip install pyserial
"""

from __future__ import annotations

import argparse
import sys
import time
from datetime import datetime, timedelta
from pathlib import Path

import serial


# ---------------------------------------------------------------------------
# Serial helpers
# ---------------------------------------------------------------------------

class Printer:
    """Thin wrapper around a pyserial port that speaks Marlin-style G-code."""

    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 1.0):
        self.ser = serial.Serial(port, baudrate, timeout=timeout)
        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()

    # -- low-level ----------------------------------------------------------

    def send(self, command: str) -> None:
        """Send a G-code line (trailing newline added automatically)."""
        self.ser.write((command + "\n").encode("ascii"))
        self.ser.flush()

    def readline(self) -> str:
        """Read one line, stripping \\r\\n.  Returns '' on timeout."""
        raw = self.ser.readline()
        return raw.decode(errors="replace").rstrip("\r\n")

    def drain(self) -> list[str]:
        """Read and return everything currently in the receive buffer."""
        lines: list[str] = []
        while self.ser.in_waiting:
            lines.append(self.readline())
        return lines

    # -- higher-level -------------------------------------------------------

    def command(self, cmd: str, timeout: float = 600.0) -> list[str]:
        """Send a G-code command and collect all output until ``ok`` is received.

        Returns every line the printer emitted *before* the ``ok`` (the ``ok``
        line itself is not included).

        Raises ``TimeoutError`` if ``ok`` is not seen within *timeout* seconds.
        """
        self.send(cmd)
        deadline = time.monotonic() + timeout
        output: list[str] = []
        while time.monotonic() < deadline:
            line = self.readline()
            if not line:
                continue
            stripped = line.strip()
            if stripped == "ok" or stripped.startswith("ok "):
                return output
            # Temperature reports, echo lines, etc. — collect them.
            output.append(line)
        raise TimeoutError(f"Timed out waiting for 'ok' after command: {cmd}")

    def wait_for_startup(self, timeout: float = 10.0) -> None:
        """Wait for the printer to finish booting (drain any startup banner)."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.readline()
            if not line:
                break
        # Give the printer a moment after boot.
        time.sleep(1.0)

    def close(self) -> None:
        self.ser.close()


# ---------------------------------------------------------------------------
# Main routine
# ---------------------------------------------------------------------------

def run_measurement(
    port: str,
    baudrate: int,
    nozzle_temp: int,
    bed_temp: int,
    duration_min: int,
    output_path: Path,
) -> None:
    printer = Printer(port, baudrate)
    try:
        printer.wait_for_startup()
        print(f"Connected to {port} @ {baudrate} baud")

        # ---- Home ----
        print("Homing (G28) …")
        printer.command("G28", timeout=120)
        print("  Homed.")

        # ---- Heat nozzle ----
        print(f"Heating nozzle to {nozzle_temp} °C (M109 S{nozzle_temp}) …")
        printer.command(f"M109 S{nozzle_temp}", timeout=600)
        print("  Nozzle at temperature.")

        # ---- Heat bed ----
        print(f"Heating bed to {bed_temp} °C (M190 S{bed_temp}) …")
        printer.command(f"M190 S{bed_temp}", timeout=600)
        print("  Bed at temperature.")

        # ---- Measurement loop ----
        end_time = datetime.now() + timedelta(minutes=duration_min)
        run_index = 0

        with output_path.open("a", encoding="utf-8") as f:
            _write_header(f, port, baudrate, nozzle_temp, bed_temp, duration_min)

            while datetime.now() < end_time:
                run_index += 1
                remaining = end_time - datetime.now()
                print(f"\n--- Run #{run_index}  (remaining {remaining}) ---")

                timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                f.write(f"\n--- Measurement #{run_index}  {timestamp} ---\n")
                f.flush()

                try:
                    lines = printer.command("M1963 A", timeout=600)
                except TimeoutError:
                    msg = f"  ERROR: M1963 timed out (run #{run_index})"
                    print(msg)
                    f.write(msg + "\n")
                    f.flush()
                    continue

                for line in lines:
                    print(line)
                    f.write(line + "\n")
                f.flush()

                # If there's less than ~2 minutes left, don't start another run
                # (a full probe takes about 1.5–2 minutes).
                if (end_time - datetime.now()).total_seconds() < 120:
                    print("\nNot enough time remaining for another run — stopping.")
                    break

        print(f"\nDone. {run_index} measurement(s) saved to {output_path}")

    finally:
        printer.close()


def _write_header(
    f,
    port: str,
    baudrate: int,
    nozzle_temp: int,
    bed_temp: int,
    duration_min: int,
) -> None:
    header = (
        "=" * 60 + "\n"
        f"  Bed Flatness Measurement Session\n"
        f"  Started : {datetime.now():%Y-%m-%d %H:%M:%S}\n"
        f"  Port    : {port} @ {baudrate} baud\n"
        f"  Nozzle  : {nozzle_temp} \u00b0C\n"
        f"  Bed     : {bed_temp} \u00b0C\n"
        f"  Duration: {duration_min} min\n"
        "=" * 60 + "\n"
    )
    f.write(header)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Automated bed flatness measurement for Prusa Buddy firmware.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "port",
        help="Serial port (e.g. /dev/ttyACM0 on Linux, COM3 on Windows)",
    )
    parser.add_argument(
        "--baud", type=int, default=115200,
        help="Baud rate (default: 115200)",
    )
    parser.add_argument(
        "--nozzle", type=int, default=170,
        help="Nozzle target temperature in °C (default: 170)",
    )
    parser.add_argument(
        "--bed", type=int, default=120,
        help="Bed target temperature in °C (default: 120)",
    )
    parser.add_argument(
        "--duration", type=int, default=25,
        help="Total measurement duration in minutes (default: 25)",
    )
    parser.add_argument(
        "--output", "-o", type=str, default=None,
        help="Output file path (default: bed_flatness_<timestamp>.txt)",
    )

    args = parser.parse_args()

    if args.output is None:
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        args.output = f"bed_flatness_{ts}.txt"

    output_path = Path(args.output)

    try:
        import serial as _serial  # noqa: F401 — just to check availability
    except ImportError:
        print("Error: pyserial is required.  Install with:  pip install pyserial",
              file=sys.stderr)
        sys.exit(1)

    try:
        run_measurement(
            port=args.port,
            baudrate=args.baud,
            nozzle_temp=args.nozzle,
            bed_temp=args.bed,
            duration_min=args.duration,
            output_path=output_path,
        )
    except serial.SerialException as exc:
        print(f"Serial error: {exc}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nInterrupted by user.")
        sys.exit(130)


if __name__ == "__main__":
    main()
