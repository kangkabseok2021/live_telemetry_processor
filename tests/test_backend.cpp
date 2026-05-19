#include "../backend/CoordTransform.h"
#include "../backend/GncValidator.h"
#include "../backend/MetricStore.h"
#include "../backend/PacketParser.h"
#include "../backend/SpscQueue.h"
#include "../backend/TelemetryFrame.h"
#include "../backend/TrajectoryPredictor.h"
#include <gtest/gtest.h>
#include <cmath>
#include <cstring>

// ── Helpers ───────────────────────────────────────────────────────────────────

static TelemetryFrame make_frame(uint32_t seq = 1) {
    TelemetryFrame f{};
    f.magic    = TelemetryFrame::kMagic;
    f.sequence = seq;
    f.timestamp_ns = 1'000'000ULL * seq;
    // Plausible ECEF near Cape Canaveral (28.4° N, 80.6° W, 0 m)
    f.pos_ecef[0] = 918'340.0;
    f.pos_ecef[1] = -5'534'340.0;
    f.pos_ecef[2] =  3'023'660.0;
    f.vel_ecef[0] = 100.0; f.vel_ecef[1] = 200.0; f.vel_ecef[2] = 300.0;
    for (int i = 0; i < 8; ++i) f.temperature[i] = 25.0f + i;
    return f;
}

// Serialize a frame back to bytes for parser tests
static std::vector<std::byte> frame_to_bytes(const TelemetryFrame& f) {
    std::vector<std::byte> buf(sizeof(f));
    std::memcpy(buf.data(), &f, sizeof(f));
    return buf;
}

// ── PacketParserTest (4) ──────────────────────────────────────────────────────

TEST(PacketParserTest, ValidFrameRoundTrip) {
    auto f = make_frame(42);
    auto bytes = frame_to_bytes(f);
    TelemetryFrame out{};
    EXPECT_EQ(parse_frame({bytes.data(), bytes.size()}, &out), ParseError::OK);
    EXPECT_EQ(out.sequence, 42u);
    EXPECT_DOUBLE_EQ(out.pos_ecef[0], f.pos_ecef[0]);
}

TEST(PacketParserTest, TruncatedFrameReturnsShortPacket) {
    auto bytes = frame_to_bytes(make_frame());
    bytes.resize(bytes.size() - 1);
    TelemetryFrame out{};
    EXPECT_EQ(parse_frame({bytes.data(), bytes.size()}, &out),
              ParseError::SHORT_PACKET);
}

TEST(PacketParserTest, BadMagicReturnsError) {
    auto f = make_frame();
    f.magic = 0xDEADBEEFu;
    auto bytes = frame_to_bytes(f);
    TelemetryFrame out{};
    EXPECT_EQ(parse_frame({bytes.data(), bytes.size()}, &out),
              ParseError::BAD_MAGIC);
}

TEST(PacketParserTest, EmptySpanReturnsShortPacket) {
    TelemetryFrame out{};
    EXPECT_EQ(parse_frame({}, &out), ParseError::SHORT_PACKET);
}

// ── GncValidatorTest (4) ──────────────────────────────────────────────────────

TEST(GncValidatorTest, FirstFrameNeverFlagged) {
    GncValidator v;
    auto r = v.validate(make_frame(1));
    EXPECT_FALSE(r.outlier);
    EXPECT_FALSE(r.seq_gap);
    EXPECT_EQ(r.missed_count, 0u);
}

TEST(GncValidatorTest, SequenceGapDetected) {
    GncValidator v;
    v.validate(make_frame(1));
    auto r = v.validate(make_frame(5));  // gap of 3
    EXPECT_TRUE(r.seq_gap);
    EXPECT_GT(r.missed_count, 0u);
}

TEST(GncValidatorTest, ConsecutiveFramesNoGap) {
    GncValidator v;
    for (uint32_t i = 1; i <= 10; ++i) {
        auto r = v.validate(make_frame(i));
        if (i > 1) EXPECT_FALSE(r.seq_gap);
    }
}

TEST(GncValidatorTest, OutlierFlaggedAfterWarmUp) {
    GncValidator v;
    // Feed 20 frames with small realistic noise so σ > 0 and the filter
    // can distinguish a genuine spike from a constant baseline.
    for (uint32_t i = 1; i <= 20; ++i) {
        auto f = make_frame(i);
        f.pos_ecef[0] += (i % 5) * 50.0;   // ±100 m variation
        v.validate(f);
    }
    // Inject a massive spike — 1e9 m is ~100 000× the noise band
    auto spike = make_frame(21);
    spike.pos_ecef[0] = 1e9;
    auto r = v.validate(spike);
    EXPECT_TRUE(r.outlier);
}

// ── CoordTransformTest (3) ────────────────────────────────────────────────────

TEST(CoordTransformTest, OriginMapsToZeroNED) {
    // Cape Canaveral approx
    CoordTransform ct(28.4, -80.6, 0.0);
    // ECEF of the origin point should map to ~[0,0,0] NED
    double ecef[3] = {918340.0, -5534340.0, 3023660.0};
    auto ned = ct.ecef_to_ned(ecef);
    // Not exact (we used approximate ECEF), allow 10 km error
    EXPECT_LT(std::abs(ned[0]), 10000.0);
    EXPECT_LT(std::abs(ned[1]), 10000.0);
}

TEST(CoordTransformTest, NorthDisplacementIsPositiveN) {
    CoordTransform ct(0.0, 0.0, 0.0);  // equatorial origin
    // Move 1000 m north in ECEF (approx: add to z-component at equator)
    double ecef_origin[3], ecef_north[3];
    // At (lat=0, lon=0), North ECEF direction is [0, 0, 1]
    ecef_origin[0] = 6378137.0; ecef_origin[1] = 0; ecef_origin[2] = 0;
    ecef_north[0]  = 6378137.0; ecef_north[1] = 0; ecef_north[2] = 1000.0;
    CoordTransform ct2(0.0, 0.0, 0.0);
    auto ned_o = ct2.ecef_to_ned(ecef_origin);
    auto ned_n = ct2.ecef_to_ned(ecef_north);
    EXPECT_GT(ned_n[0] - ned_o[0], 900.0);  // North component increases
}

TEST(CoordTransformTest, VelocityTransformSameDimension) {
    CoordTransform ct(28.4, -80.6, 0.0);
    double vel[3] = {100.0, 200.0, 300.0};
    auto ned_v = ct.vel_ecef_to_ned(vel);
    // Magnitude preserved
    double mag_in  = std::sqrt(100*100 + 200*200 + 300*300);
    double mag_out = std::sqrt(ned_v[0]*ned_v[0] +
                               ned_v[1]*ned_v[1] +
                               ned_v[2]*ned_v[2]);
    EXPECT_NEAR(mag_in, mag_out, 1e-6);
}

// ── TrajectoryPredictorTest (2) ───────────────────────────────────────────────

TEST(TrajectoryPredictorTest, EmptyPredictorReturnsZero) {
    TrajectoryPredictor tp;
    auto p = tp.predict(0.5);
    EXPECT_DOUBLE_EQ(p[0], 0.0);
}

TEST(TrajectoryPredictorTest, LinearTrajectoryPredicted) {
    TrajectoryPredictor tp;
    // Feed 20 points on a straight line: north increases by 100 m/sample
    for (int i = 0; i < 20; ++i)
        tp.push({static_cast<double>(i) * 100.0, 0.0, 0.0});
    // Predict 1 sample ahead → expect ~2000 m north
    auto p = tp.predict(1.0);
    EXPECT_NEAR(p[0], 2000.0, 200.0);
}

// ── MetricStoreTest (2) ───────────────────────────────────────────────────────

TEST(MetricStoreTest, UpdateAndSnapshotRoundTrip) {
    MetricStore store;
    TelemetrySnapshot snap;
    snap.frame = make_frame(99);
    snap.ned_pos = {100.0, 200.0, -50.0};
    snap.state = PipelineState::LINK_OK;
    store.update(snap);
    auto got = store.snapshot();
    EXPECT_EQ(got.frame.sequence, 99u);
    EXPECT_DOUBLE_EQ(got.ned_pos[0], 100.0);
    EXPECT_EQ(got.state, PipelineState::LINK_OK);
}

TEST(MetricStoreTest, StateAtomicReadable) {
    MetricStore store;
    store.set_state(PipelineState::MISSION_ABORT);
    EXPECT_EQ(store.state(), PipelineState::MISSION_ABORT);
}
