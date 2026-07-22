#include "state_machine_test_common.hpp"

TEST(StateMachineEdgeTest, WhenUnhandledTriggerIsFired_ProvidedHandlerIsCalled)
{
    auto sm = MakeSimpleMachine(State::B);

    State capturedState{};
    Trigger capturedTrigger{};
    sm.OnUnhandledTrigger([&](const State& s, const Trigger& t, const std::vector<std::string>&) {
        capturedState = s;
        capturedTrigger = t;
    });

    sm.Trigger(Trigger::Z);

    EXPECT_EQ(State::B, capturedState);
    EXPECT_EQ(Trigger::Z, capturedTrigger);
}

TEST(StateMachineEdgeTest, QueuedMode_ReentrantFireInEntryAction_IsProcessed)
{
    auto sm = MakeSimpleMachine(State::A);

    sm.Configure(State::A).Permit(Trigger::X, State::B);
    sm.Configure(State::B)
        .Permit(Trigger::Y, State::C)
        .OnEntry([&]() { sm.Trigger(Trigger::Y); });

    sm.Trigger(Trigger::X);

    EXPECT_EQ(State::C, sm.State());
}

TEST(StateMachineEdgeTest, ImmediateMode_ReentrantFireInEntryAction_IsProcessed)
{
    StateMachine<State, Trigger> sm(State::A, FiringMode::Immediate);

    sm.Configure(State::A).Permit(Trigger::X, State::B);
    sm.Configure(State::B)
        .Permit(Trigger::Y, State::C)
        .OnEntry([&]() { sm.Trigger(Trigger::Y); });

    sm.Trigger(Trigger::X);

    EXPECT_EQ(State::C, sm.State());
}
