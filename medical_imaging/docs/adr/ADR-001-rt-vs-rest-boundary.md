# ADR-001: RT vs REST Boundary

**Date:** 2026-05-21
**Status:** Accepted
**Deciders:** Imaging Engine Team

---

## Context

The medical imaging engine has two distinct classes of external consumers:

1. **RT diagnostic client (C# GUI):** needs to poll state and inject events at sub-millisecond latency for closed-loop control (e.g., stopping acquisition on fault detection).
2. **Telemetry dashboard:** needs timing histograms, transition counts, and health metrics for operator monitoring. Round-trip latencies of 400–800 ms are fully acceptable here.

We need to decide: should the boundary between C++ and C# use P/Invoke (direct in-process call) or a REST/gRPC interface?

---

## Decision

Use the **extern-C P/Invoke shim** (`c_api.h` → `libimaging_engine.so`) for all RT control and state queries.

Use a separate REST endpoint (outside this module's scope) for telemetry export, metrics, and non-latency-sensitive data.

---

## Rationale

| Criterion | P/Invoke | REST |
|---|---|---|
| Round-trip latency | < 3 µs (in-process, no serialisation) | 400–800 µs (network stack + JSON) |
| Implementation complexity | Low — one `DllImport` per function | Medium — HTTP client + serialisation + error handling |
| Marshalling overhead | Trivial (`int`, `uint64_t`) | JSON encode/decode (heap allocations) |
| Fault isolation | Same process — crash propagates | Separate process — fault contained |
| Versioning | ABI pinned by `c_api.h` | API versioned via URL / Content-Type |
| Testing | Unit-testable with mock native | Requires mock HTTP server |

For RT control, the latency advantage of P/Invoke (< 3 µs) vastly outweighs the fault-isolation benefit of REST. A C# crash that propagates into the native engine is already a hard stop — there is no benefit to REST isolation for this path.

Telemetry data is not latency-sensitive and benefits from REST's loose coupling, language independence, and fire-and-forget delivery semantics.

---

## Consequences

### Positive
- Sub-microsecond state queries and event injection from C#.
- No serialisation allocations on the RT control path.
- `c_api.h` is a minimal, stable ABI surface.

### Negative / Risks
- `libimaging_engine.so` must be deployed alongside the C# host process (native dependency).
- Breaking changes to `c_api.h` require a major version bump and C# recompilation.
- P/Invoke requires matching platform targets (x64 vs ARM64); a fat binary or separate package per arch is needed for cross-platform deployments.

### Mitigations
- `c_api.h` is versioned with `IMAGING_API_VERSION` (future enhancement).
- CI tests both x86_64 (Ubuntu 22.04) and ARM64 builds.

---

## Alternatives Considered

**gRPC (protobuf):** Lower latency than REST (~50 µs) but still ~20× slower than P/Invoke. Adds a significant build dependency.

**Shared memory + named pipe:** Comparable latency to P/Invoke but more complex synchronisation and no natural C# abstraction.

**COM/DCOM (Windows only):** Out of scope for a Linux-first deployment.
