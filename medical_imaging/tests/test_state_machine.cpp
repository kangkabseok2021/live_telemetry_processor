#include <gtest/gtest.h>
#include "StateMachine.h"
#include "TransitionLog.h"
#include <chrono>
#include <thread>

using namespace imaging;

// Setup helper: drive SM through a transition, discarding result (for setup steps)
static void step(StateMachine& sm, Event e) {
    (void)sm.transition(e);
}

// ── Suite 1: Valid transitions ────────────────────────────────────────────

TEST(StateMachineTest, IdleToCalibrating_OnCmdStart) {
    StateMachine sm;
    auto result = sm.transition(CmdStart{});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<Calibrating>(result.value()));
    EXPECT_EQ(std::get<Calibrating>(result.value()).steps_remaining,
              static_cast<int>(CALIB_STEPS));
}

TEST(StateMachineTest, CalibratingToAcquiring_OnCalibDone) {
    StateMachine sm;
    step(sm, CmdStart{});
    auto result = sm.transition(EvtCalibDone{});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<Acquiring>(result.value()));
    EXPECT_EQ(std::get<Acquiring>(result.value()).frame_count, 0u);
}

TEST(StateMachineTest, AcquiringToProcessing_OnBatchReady) {
    StateMachine sm;
    step(sm, CmdStart{});
    step(sm, EvtCalibDone{});
    auto result = sm.transition(EvtBatchReady{5});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<Processing>(result.value()));
    EXPECT_EQ(std::get<Processing>(result.value()).batch_size, 5u);
}

TEST(StateMachineTest, ProcessingToIdle_OnProcessDone) {
    StateMachine sm;
    step(sm, CmdStart{});
    step(sm, EvtCalibDone{});
    step(sm, EvtBatchReady{5});
    auto result = sm.transition(EvtProcessDone{});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<Idle>(result.value()));
}

// ── Suite 2: Invalid transitions ─────────────────────────────────────────

TEST(StateMachineTest, CalibratingToProcessing_DirectBlocked) {
    StateMachine sm;
    step(sm, CmdStart{});  // now Calibrating
    auto result = sm.transition(EvtBatchReady{3});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), TransitionError::INVALID_TRANSITION);
}

TEST(StateMachineTest, AcquiringToIdle_DirectBlocked) {
    StateMachine sm;
    step(sm, CmdStart{});
    step(sm, EvtCalibDone{});  // now Acquiring
    auto result = sm.transition(EvtProcessDone{});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), TransitionError::INVALID_TRANSITION);
}

TEST(StateMachineTest, FaultToCalibrating_DirectBlocked) {
    StateMachine sm;
    step(sm, EvtError{"test"});  // now Fault
    auto result = sm.transition(CmdStart{});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), TransitionError::INVALID_TRANSITION);
}

// ── Suite 3: Fault handling ───────────────────────────────────────────────

TEST(StateMachineTest, AnyStateToFault_FromIdle) {
    StateMachine sm;
    auto result = sm.transition(EvtError{"hardware fault"});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<Fault>(result.value()));
    EXPECT_EQ(std::get<Fault>(result.value()).reason, "hardware fault");
}

TEST(StateMachineTest, AnyStateToFault_FromCalibrating) {
    StateMachine sm;
    step(sm, CmdStart{});
    auto result = sm.transition(EvtError{"sensor error"});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<Fault>(result.value()));
}

TEST(StateMachineTest, AnyStateToFault_FromAcquiring) {
    StateMachine sm;
    step(sm, CmdStart{});
    step(sm, EvtCalibDone{});
    auto result = sm.transition(EvtError{"frame overrun"});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<Fault>(result.value()));
}

TEST(StateMachineTest, FaultToIdle_OnCmdReset) {
    StateMachine sm;
    step(sm, EvtError{"test"});
    auto result = sm.transition(CmdReset{});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<Idle>(result.value()));
}

// ── Suite 4: Infrastructure ───────────────────────────────────────────────

TEST(StateMachineTest, DeadlineNotExceeded_NormalTransition) {
    StateMachine sm;
    step(sm, CmdStart{});
    EXPECT_EQ(sm.deadlineViolations(), 0u);
}

TEST(StateMachineTest, TransitionLog_PopulatedAfterTransition) {
    StateMachine sm;
    uint32_t before = g_log.count();
    step(sm, CmdStart{});
    EXPECT_GT(g_log.count(), before);
}

TEST(StateMachineTest, TransitionLog_RingWraps_At_1024) {
    StateMachine sm;
    // Pump 512 pairs of EvtError + CmdReset = 1024 transitions
    for (int i = 0; i < 512; ++i) {
        step(sm, EvtError{"x"});  // → Fault
        step(sm, CmdReset{});     // → Idle
    }
    // count must not exceed LOG_CAPACITY
    EXPECT_LE(g_log.count(), LOG_CAPACITY);
    // reading index 0 must not crash
    auto entry = g_log.get(0);
    (void)entry;
}
