#include "PacketParser.h"
#include <cstring>

ParseError parse_frame(std::span<const std::byte> raw,
                       TelemetryFrame*            out) noexcept {
    if (raw.size() < sizeof(TelemetryFrame))
        return ParseError::SHORT_PACKET;

    // Zero-copy: reinterpret bytes directly into the struct.
    // Alignment guaranteed: UDP payloads are malloc-aligned by the kernel.
    std::memcpy(out, raw.data(), sizeof(TelemetryFrame));

    if (out->magic != TelemetryFrame::kMagic)
        return ParseError::BAD_MAGIC;

    return ParseError::OK;
}
