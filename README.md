# Deterministic Live Telemetry Processor & Mission Visualization Pipeline

A ground-station software stack for processing high-frequency aerospace telemetry in real time. A **C++20 backend** receives binary GNC state vectors over UDP at 1000+ Hz and feeds them through a **lock-free SPSC pipeline** into a **zero-copy binary parser**. A **GNC numerical validation layer** applies moving-window deviation filters and detects sequence gaps. A **coordinate transformation engine** converts ECEF positions to local NED. A **Qt 6 operator UI** renders a live 2D trajectory at 60 Hz via a throttled render pipeline that never blocks the hot ingestion path. A **Python HIL simulator** replays captured flight logs for end-to-end mission rehearsal.

---

## Architecture

```
  UDP Socket (port 57300)
  POSIX SO_RCVBUF 4 MB, SCHED_FIFO thread
          │
          ▼
  SpscQueue<TelemetryFrame, 4096>        ← lock-free, acquire/release
          │
          ▼  (processing jthread — stop_token cooperative shutdown)
  PacketParser::parse_frame()            ← zero-copy span<const byte> → struct
          │
          ├─► GncValidator               ← O(1) moving-window σ, seq gap check
          │
          ├─► CoordTransform             ← ECEF → NED (WGS-84 rotation matrix)
          │
          ├─► TrajectoryPredictor        ← 2nd-order least-squares, 500 ms ahead
          │
          ▼
  MetricStore  (std::shared_mutex)       ← thread-safe telemetry snapshot
          │
          ▼  (QTimer 60 Hz — GUI thread only)
  MainWindow::onRenderTick()
          ├─► TrajectoryWidget           ← QPainter 2D NED plot, 10 000 pts
          └─► StatusPanel               ← pipeline state, GNC readouts
```

### Key Design Properties

| Property | Mechanism |
|---|---|
| < 2 ms ingestion latency | `SCHED_FIFO` UDP thread, `SO_RCVBUF` 4 MB |
| Zero packet contention | Lock-free SPSC — no mutex on the hot path |
| Zero-copy parsing | `std::span<const std::byte>` cast to `#pragma pack(1)` struct |
| UI never blocks backend | `QueuedConnection` + 60 Hz `QTimer` reads `MetricStore` snapshot |
| Cooperative shutdown | `std::jthread` + `std::stop_token` — no raw flags, no `join()` |
| Testable without Qt | `backend_lib` has no Qt dependency — tests compile without Qt6 |

---

## Telemetry Frame Format

120-byte binary packet (little-endian), matching the Python HIL simulator:

| Offset | Size | Field | Unit |
|---|---|---|---|
| 0 | 4 B | magic `0x41455243` ("AERC") | — |
| 4 | 4 B | sequence (monotonic) | — |
| 8 | 8 B | timestamp_ns | ns |
| 16 | 24 B | pos_ecef[3] (X, Y, Z) | m |
| 40 | 24 B | vel_ecef[3] (Vx, Vy, Vz) | m/s |
| 64 | 32 B | temperature[8] | °C |
| 96 | 24 B | traj_ned[3] (N, E, D) | m |

---

## GNC Numerical Validation

**Moving-window σ filter** (`RollingStats<N=50>`, O(1) update):

- Running sum and sum-of-squares in a circular buffer
- Samples exceeding `k·σ` (default k = 4.0) flagged as `SENSOR_OUTLIER`
- Requires ≥ 10 warm-up samples before flagging to avoid transient false alarms
- Applied to all 6 GNC channels: position (× 3) and velocity (× 3)

**Sequence gap detection:**

- Gap of 1–10 → increments `missed_count`, sets `seq_gap` flag
- Gap > 10 → pipeline transitions to `LINK_DEGRADED`
- > 200 consecutive outlier frames → `SENSOR_OUTLIER`

**Pipeline states:**

```
INIT → LINK_OK → LINK_DEGRADED
                → SENSOR_OUTLIER
                → MISSION_ABORT  (latching, operator-cleared)
```

---

## Coordinate Transform

ECEF → NED using the standard WGS-84 rotation matrix parameterised by geodetic latitude φ and longitude λ of the launch-site origin:

```
NED = R(φ, λ) × (P_ecef − P_origin)

R = ⎡ −sin φ cos λ   −sin φ sin λ    cos φ ⎤   ← North row
    ⎢ −sin λ          cos λ           0     ⎥   ← East row
    ⎣ −cos φ cos λ   −cos φ sin λ   −sin φ  ⎦   ← Down row
```

Origin defaults to Cape Canaveral (28.4° N, 80.6° W). Configurable at runtime.

---

## Trajectory Predictor

Fits a 2nd-order polynomial to the last 100 NED position samples using least-squares:

```
p(t) = c₀ + c₁·t + c₂·t²
```

Solved via Gaussian elimination on the 3×3 normal equations `(AᵀA)c = Aᵀy`. Projects 500 ms ahead and displays the predicted position as an overlay on the Qt trajectory plot.

---

## Project Layout

```
live_telemetry_processor/
├── backend/
│   ├── TelemetryFrame.h/cpp      120-byte packet struct + PipelineState enum
│   ├── SpscQueue.h               Lock-free SPSC ring buffer (header-only)
│   ├── PacketParser.h/cpp        Zero-copy UDP frame parser
│   ├── GncValidator.h/cpp        Moving-window σ filter + seq gap detection
│   ├── CoordTransform.h/cpp      WGS-84 ECEF → NED rotation matrix
│   ├── TrajectoryPredictor.h/cpp 2nd-order least-squares trajectory forecast
│   ├── MetricStore.h/cpp         std::shared_mutex telemetry snapshot
│   ├── UdpReceiver.h/cpp         jthread UDP socket, SCHED_FIFO, SO_RCVBUF
│   └── main_headless.cpp         Headless backend runner (no Qt)
├── ui/
│   ├── TrajectoryWidget.h/cpp    QPainter 2D NED plot, 10 000-point history
│   ├── StatusPanel.h/cpp         Colour-coded state + GNC numeric readouts
│   ├── MainWindow.h/cpp          60 Hz QTimer, QueuedConnection, proc jthread
│   └── main_ui.cpp               Qt application entry point
├── tests/
│   └── test_backend.cpp          15 GoogleTests (no Qt required)
├── hil/
│   ├── hil_sim.py                Python UDP flight-log replayer + fault injection
│   └── test_hil_sim.py           5 pytest tests for packet format correctness
├── CMakeLists.txt
├── Dockerfile                    gcc:14 builder → debian:bookworm-slim runtime
├── Dockerfile.hil                Python HIL simulator container
├── docker-compose.yml
├── pyproject.toml
└── .github/workflows/ci.yml
```

---

## Quick Start

### Build and test (Linux/macOS, requires cmake + g++14+)

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j4 --target backend_lib test_backend
ctest --test-dir build --output-on-failure -V
```

### Run the headless backend

```bash
cmake --build build --target telemetry_headless
./build/telemetry_headless 57300   # listens on UDP :57300
```

### Run the Qt 6 UI (requires Qt6 Widgets)

```bash
# Qt6 is found automatically if installed
cmake --build build --target telemetry_node
./build/telemetry_node --port 57300
```

### Stream test data with the HIL simulator

```bash
uv sync
uv run python -m hil.hil_sim --rate 1000 --duration 30
# Inject a sequence gap at t=10 s:
uv run python -m hil.hil_sim --rate 100 --fault gap --fault-at 10.0
# Inject an outlier spike at t=5 s:
uv run python -m hil.hil_sim --rate 100 --fault outlier --fault-at 5.0
```

### Run with Docker Compose

```bash
docker compose up --build
```

---

## Tests

### C++ — 15 GoogleTests (`backend_lib`, no Qt)

| Suite | Test | What it verifies |
|---|---|---|
| PacketParserTest | ValidFrameRoundTrip | Magic, sequence, ECEF pos survive parse |
| PacketParserTest | TruncatedFrameReturnsShortPacket | `n-1` bytes → `SHORT_PACKET` |
| PacketParserTest | BadMagicReturnsError | Wrong magic → `BAD_MAGIC` |
| PacketParserTest | EmptySpanReturnsShortPacket | Zero bytes → `SHORT_PACKET` |
| GncValidatorTest | FirstFrameNeverFlagged | First frame always accepted |
| GncValidatorTest | SequenceGapDetected | Skip 3 seq numbers → gap flag |
| GncValidatorTest | ConsecutiveFramesNoGap | Frames 1–10 → no gap flag |
| GncValidatorTest | OutlierFlaggedAfterWarmUp | 1e9 m spike after 20 noisy frames |
| CoordTransformTest | OriginMapsToZeroNED | ECEF origin → NED near zero |
| CoordTransformTest | NorthDisplacementIsPositiveN | +1000 m ECEF-Z → positive N |
| CoordTransformTest | VelocityTransformSameDimension | Rotation preserves magnitude |
| TrajectoryPredictorTest | EmptyPredictorReturnsZero | No data → zero prediction |
| TrajectoryPredictorTest | LinearTrajectoryPredicted | 20 collinear pts → correct extrapolation |
| MetricStoreTest | UpdateAndSnapshotRoundTrip | snapshot() returns latest update |
| MetricStoreTest | StateAtomicReadable | state() reflects set_state() immediately |

### Python — 5 pytest tests (`hil/test_hil_sim.py`)

| Test | What it verifies |
|---|---|
| test_frame_size | Packed struct is exactly 120 bytes |
| test_magic_correct | Magic bytes match C++ `kMagic` constant |
| test_sequence_encoded | Sequence number survives struct packing |
| test_timestamp_nonzero | Non-zero timestamp for t > 0 |
| test_outlier_fault_inflates_pos | Outlier fault multiplies pos by 1000× |

---

## CI

| Job | Runner | What it does |
|---|---|---|
| `cpp-build-test` | ubuntu-latest | CMake build + 15 GoogleTests (no Qt) |
| `python-hil-tests` | ubuntu-latest | 5 pytest tests for HIL simulator |
| `arm64-cross-build` | ubuntu-latest | Cross-compile `backend_lib` for aarch64 |
| `docker-build` | ubuntu-latest | `docker build --target builder` smoke test |

---

## HIL Simulator

`hil/hil_sim.py` generates synthetic `TelemetryFrame` packets matching the C++ struct layout (verified by `test_frame_size`) and transmits them over UDP.

**Fault injection modes:**

| Mode | Flag | Effect |
|---|---|---|
| Sequence gap | `--fault gap --fault-at T` | Skips 15 sequence numbers at T seconds → triggers `LINK_DEGRADED` |
| Sensor outlier | `--fault outlier --fault-at T` | Multiplies `pos_ecef[0]` by 1000 at T seconds → triggers `SENSOR_OUTLIER` |

---

## Tech Stack

| Layer | Technology |
|---|---|
| Language | C++20 — jthread, stop_token, span, atomic acquire/release |
| Networking | POSIX UDP socket, SO_RCVBUF, SCHED_FIFO |
| Concurrency | Lock-free SPSC, std::shared_mutex, std::jthread |
| Parsing | Zero-copy std::span → #pragma pack(1) struct |
| Signal processing | O(1) moving-window σ filter, least-squares trajectory predictor |
| Coordinate math | WGS-84 ECEF → NED rotation matrix |
| Qt UI | Qt 6 Widgets, QPainter, QTimer 60 Hz, QueuedConnection |
| HIL simulator | Python 3.12, asyncio-timed UDP, struct.pack |
| Container | Docker multi-stage (gcc:14 → debian:bookworm-slim) |
| CI | GitHub Actions: build+test, Python HIL, ARM64 cross-compile, Docker |
| Targets | x86\_64 (CI/dev), ARM64 (Raspberry Pi 4 / edge ground station) |
