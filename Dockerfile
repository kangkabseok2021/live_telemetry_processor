# Stage 1 — Build
FROM gcc:14-bookworm AS builder

RUN apt-get update && \
    apt-get install -y --no-install-recommends cmake && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -B build -S . \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_POLICY_DEFAULT_CMP0135=NEW \
      -DBUILD_TESTING=OFF && \
    cmake --build build --target telemetry_headless -j"$(nproc)"

# Stage 2 — Minimal runtime (headless backend)
FROM debian:bookworm-slim

RUN useradd --system --no-create-home --uid 1001 telemetry

COPY --from=builder /src/build/telemetry_headless /usr/local/bin/telemetry_headless

USER telemetry
EXPOSE 57300/udp

ENTRYPOINT ["/usr/local/bin/telemetry_headless"]
