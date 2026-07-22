#include "state_machine_test_common.hpp"

TEST(StateMachineErrorTest, ExceptionThrownForInvalidTransition)
{
    auto sm = MakeSimpleMachine(State::A);
    EXPECT_THROW(sm.Trigger(Trigger::X), std::logic_error);
}

TEST(StateMachineErrorTest, ImplicitReentryIsDisallowed)
{
    auto sm = MakeSimpleMachine(State::B);
    EXPECT_THROW(sm.Configure(State::B).Permit(Trigger::X, State::B), std::invalid_argument);
}

TEST(StateMachineErrorTest, DirectCyclicConfigurationDetected)
{
    auto sm = MakeSimpleMachine(State::A);
    EXPECT_THROW(sm.Configure(State::A).SubstateOf(State::A), std::invalid_argument);
}

TEST(StateMachineErrorTest, NestedCyclicConfigurationDetected)
{
    auto sm = MakeSimpleMachine(State::A);
    sm.Configure(State::B).SubstateOf(State::A);
    EXPECT_THROW(sm.Configure(State::A).SubstateOf(State::B), std::invalid_argument);
}

TEST(StateMachineErrorTest, NestedTwoLevelsCyclicConfigurationDetected)
{
    auto sm = MakeSimpleMachine(State::A);
    sm.Configure(State::B).SubstateOf(State::A);
    sm.Configure(State::C).SubstateOf(State::B);
    EXPECT_THROW(sm.Configure(State::A).SubstateOf(State::C), std::invalid_argument);
}

TEST(StateMachineErrorTest, TriggerParametersImmutableOnceSet)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.SetTriggerParameters<std::string, int>(Trigger::X);
    EXPECT_THROW((sm.SetTriggerParameters<std::string>(Trigger::X)), std::logic_error);
}

TEST(StateMachineErrorTest, InitialTransitionTargetMustBeSubstate)
{
    auto sm = MakeSimpleMachine(State::A);

    sm.Configure(State::C).InitialTransition(State::B);
    sm.Configure(State::A).Permit(Trigger::X, State::C);

    EXPECT_THROW(sm.Trigger(Trigger::X), std::logic_error);
}

TEST(StateMachineErrorTest, FireWithConfiguredTrigger_ThrowsWhenParameterCountMismatch)
{
    auto sm = MakeSimpleMachine(State::A);
    auto x = sm.SetTriggerParameters<std::string, int>(Trigger::X);
    sm.Configure(State::A).Permit(Trigger::X, State::B);

    StateMachine<State, Trigger>::Args wrongArgs{std::string("only one")};
    const StateMachine<State, Trigger>::TriggerWithParameters& trigger = x;

    EXPECT_THROW(sm.Trigger(trigger, wrongArgs), std::invalid_argument);
}

TEST(StateMachineErrorTest, FireWithConfiguredTrigger_ThrowsWhenParameterTypeMismatch)
{
    auto sm = MakeSimpleMachine(State::A);
    auto x = sm.SetTriggerParameters<std::string, int>(Trigger::X);
    sm.Configure(State::A).Permit(Trigger::X, State::B);

    StateMachine<State, Trigger>::Args wrongArgs{42, 7};
    const StateMachine<State, Trigger>::TriggerWithParameters& trigger = x;

    EXPECT_THROW(sm.Trigger(trigger, wrongArgs), std::invalid_argument);
}

TEST(StateMachineErrorTest, MultiplePermittedTransitionsForSameTrigger_Throws)
{
    auto sm = MakeSimpleMachine(State::A);
    sm.Configure(State::A)
        .PermitIf(Trigger::X, State::B, []() { return true; })
        .PermitIf(Trigger::X, State::C, []() { return true; });

    EXPECT_THROW(sm.Trigger(Trigger::X), std::logic_error);
}
