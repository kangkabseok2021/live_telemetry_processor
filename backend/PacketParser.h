#pragma once
#include "TelemetryFrame.h"
#include <cstdint>
#include <span>

enum class ParseError { OK, SHORT_PACKET, BAD_MAGIC };

// Zero-copy binary parser.
// Interprets raw UDP payload bytes as TelemetryFrame without memcpy.
// Returns ParseError::OK and fills *out on success.
ParseError parse_frame(std::span<const std::byte> raw,
                       TelemetryFrame*            out) noexcept;
