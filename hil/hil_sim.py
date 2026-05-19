"""HIL (Hardware-in-the-Loop) telemetry simulator.

Generates synthetic TelemetryFrame packets matching the C++ struct layout
and transmits them over UDP at a configurable rate.  Supports fault injection:
sequence gaps, sensor outliers, and link dropout windows.

Usage:
    python hil_sim.py [--host HOST] [--port PORT] [--rate HZ] [--duration S]
    python hil_sim.py --fault gap --fault-at 5.0
    python hil_sim.py --fault outlier --fault-at 3.0
"""

from __future__ import annotations

import argparse
import math
import socket
import struct
import sys
import time

# Must match TelemetryFrame in backend/TelemetryFrame.h
# < = little-endian
# I = uint32, Q = uint64, 3d = three doubles, 8f = eight floats, 3d = three doubles
FRAME_FMT  = "<IIQ" + "6d" + "8f" + "3d"
FRAME_SIZE = struct.calcsize(FRAME_FMT)
MAGIC      = 0x41455243  # "AERC"

assert FRAME_SIZE == 120, f"Frame size mismatch: {FRAME_SIZE}"


def make_frame(seq: int, t: float, fault: str | None = None) -> bytes:
    """Build one TelemetryFrame packet."""
    ts_ns = int(t * 1e9)

    # Simulated trajectory: parabolic arc from Cape Canaveral
    # ECEF origin: (918340, -5534340, 3023660) m
    vx, vy, vz = 200.0, 100.0, 500.0 + math.sin(t) * 50.0
    x = 918340.0  + vx * t
    y = -5534340.0 + vy * t
    z = 3023660.0  + vz * t - 0.5 * 9.8 * t * t  # gravity

    if fault == "outlier":
        x *= 1000.0   # massive spike

    temps = tuple(25.0 + i * 0.5 + math.sin(t + i) for i in range(8))
    # Pre-computed NED (simplified: just use relative displacement)
    traj_n = vx * t
    traj_e = vy * t
    traj_d = -(vz * t - 0.5 * 9.8 * t * t)

    return struct.pack(FRAME_FMT,
        MAGIC, seq, ts_ns,
        x, y, z, vx, vy, vz,  # pos_ecef + vel_ecef
        *temps,                 # temperature[8]
        traj_n, traj_e, traj_d # traj_ned
    )


def run(host: str, port: int, rate: float, duration: float,
        fault: str | None, fault_at: float) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    interval = 1.0 / rate
    seq      = 1
    t_start  = time.monotonic()
    sent     = 0
    dropped  = 0

    print(f"HIL sim → {host}:{port}  rate={rate} Hz  duration={duration} s")
    if fault:
        print(f"  Fault injection: {fault} at t={fault_at} s")

    deadline = time.monotonic()
    while True:
        now = time.monotonic()
        elapsed = now - t_start
        if elapsed >= duration:
            break

        deadline += interval
        sleep_s = deadline - time.monotonic()
        if sleep_s > 0:
            time.sleep(sleep_s)

        inject = fault and abs(elapsed - fault_at) < interval * 2

        if fault == "gap" and inject:
            # Skip 15 sequence numbers to trigger LINK_DEGRADED
            seq += 15
            dropped += 15
            continue

        active_fault = "outlier" if (fault == "outlier" and inject) else None
        payload = make_frame(seq, elapsed, active_fault)
        sock.sendto(payload, (host, port))
        seq  += 1
        sent += 1

    sock.close()
    print(f"Sent {sent} frames, simulated {dropped} drops in {duration:.1f} s")


def main() -> None:
    p = argparse.ArgumentParser(description="HIL telemetry simulator")
    p.add_argument("--host",     default="127.0.0.1")
    p.add_argument("--port",     type=int,   default=57300)
    p.add_argument("--rate",     type=float, default=100.0,
                   help="Packet rate in Hz (default 100)")
    p.add_argument("--duration", type=float, default=10.0,
                   help="Simulation duration in seconds")
    p.add_argument("--fault",    choices=["gap", "outlier"],
                   help="Fault type to inject")
    p.add_argument("--fault-at", type=float, default=5.0,
                   help="Elapsed time (s) at which to inject the fault")
    args = p.parse_args()

    run(args.host, args.port, args.rate, args.duration,
        args.fault, args.fault_at)


if __name__ == "__main__":
    main()
