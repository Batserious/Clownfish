/**
 * @file state_machine_test.cpp
 * @brief Unit tests for Clownfish::StateMachine, ported from Stateless C# test suite.
 *
 * Original C# tests: stateless/test/Stateless.Tests/StateMachineFixture.cs
 */

#include <gtest/gtest.h>
#include "Clownfish/Clownfish.hpp"
#include <string>
#include <vector>
#include <algorithm>

using namespace Clownfish;

// ---------------------------------------------------------------------------
// Common enums used across tests (mirrors State.cs / Trigger.cs in C# tests)
// ---------------------------------------------------------------------------
enum class State { A, B, C };
enum class Trigger { X, Y, Z };

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------
static StateMachine<State, Trigger> MakeSimpleMachine(const State initial = State::B)
{
    return StateMachine<State, Trigger>(initial);
}

// ===========================================================================
// Basic transitions
// ===========================================================================

TEST(StateMachineTest, InitialStateIsCurrent)
{
    auto sm = MakeSimpleMachine(State::B);
    EXPECT_EQ(State::B, sm.State());
}

TEST(StateMachineTest, CanUseEnumMarkers)
{
    auto sm = MakeSimpleMachine(State::A);
    sm.Configure(State::A).Permit(Trigger::X, State::B);
    sm.Fire(Trigger::X);
    EXPECT_EQ(State::B, sm.State());
}

TEST(StateMachineTest, StateCanBeStoredExternally)
{
    State state = State::B;
    StateMachine<State, Trigger> sm(
        [&]() { return state; },
        [&](const State& s) { state = s; });
    sm.Configure(State::B).Permit(Trigger::X, State::C);
    EXPECT_EQ(State::B, sm.State());
    EXPECT_EQ(State::B, state);
    sm.Fire(Trigger::X);
    EXPECT_EQ(State::C, sm.State());
    EXPECT_EQ(State::C, state);
}

TEST(StateMachineTest, StateMutatorCalledOnlyOnce)
{
    State state = State::B;
    int count = 0;
    StateMachine<State, Trigger> sm(
        [&]() { return state; },
        [&](const State& s) { state = s; ++count; });
    sm.Configure(State::B).Permit(Trigger::X, State::C);
    sm.Fire(Trigger::X);
    EXPECT_EQ(1, count);
}

// ===========================================================================
// Substate / superstate
// ===========================================================================

TEST(StateMachineTest, SubstateIsIncludedInCurrentState)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.Configure(State::B).SubstateOf(State::C);
    EXPECT_EQ(State::B, sm.State());
    EXPECT_TRUE(sm.IsInState(State::C));
}

TEST(StateMachineTest, WhenInSubstate_TriggerIgnoredInSuperstate_RemainsInSubstate)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.Configure(State::B).SubstateOf(State::C);
    sm.Configure(State::C).Ignore(Trigger::X);
    sm.Fire(Trigger::X);
    EXPECT_EQ(State::B, sm.State());
}

TEST(StateMachineTest, WhenInSubstate_TriggerSuperStateTwice_DoesNotReenterSubstate)
{
    auto sm = MakeSimpleMachine(State::A);
    int eCount = 0;

    sm.Configure(State::B)
        .OnEntry([&]() { eCount++; })
        .SubstateOf(State::C);

    sm.Configure(State::A)
        .SubstateOf(State::C);

    sm.Configure(State::C)
        .Permit(Trigger::X, State::B);

    sm.Fire(Trigger::X);
    sm.Fire(Trigger::X);

    EXPECT_EQ(1, eCount);
}

TEST(StateMachineTest, PermittedTriggersIncludeSuperstatePermittedTriggers)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.Configure(State::A).Permit(Trigger::Z, State::B);
    sm.Configure(State::B).SubstateOf(State::C).Permit(Trigger::X, State::A);
    sm.Configure(State::C).Permit(Trigger::Y, State::A);

    auto permitted = sm.GetPermittedTriggers();

    EXPECT_TRUE(std::find(permitted.begin(), permitted.end(), Trigger::X) != permitted.end());
    EXPECT_TRUE(std::find(permitted.begin(), permitted.end(), Trigger::Y) != permitted.end());
    EXPECT_FALSE(std::find(permitted.begin(), permitted.end(), Trigger::Z) != permitted.end());
}

TEST(StateMachineTest, PermittedTriggersAreDistinct)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.Configure(State::B).SubstateOf(State::C).Permit(Trigger::X, State::A);
    sm.Configure(State::C).Permit(Trigger::X, State::B);

    auto permitted = sm.GetPermittedTriggers();
    // X should appear only once
    int count = static_cast<int>(std::count(permitted.begin(), permitted.end(), Trigger::X));
    EXPECT_EQ(1, count);
}

// ===========================================================================
// Guards (PermitIf)
// ===========================================================================

TEST(StateMachineTest, AcceptedTriggersRespectGuards)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.Configure(State::B).PermitIf(Trigger::X, State::A, []() { return false; });
    auto permitted = sm.GetPermittedTriggers();
    EXPECT_EQ(0u, permitted.size());
}

TEST(StateMachineTest, WhenDiscriminatedByGuard_ChoosesPermittedTransition)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.Configure(State::B)
        .PermitIf(Trigger::X, State::A, []() { return false; })
        .PermitIf(Trigger::X, State::C, []() { return true; });
    sm.Fire(Trigger::X);
    EXPECT_EQ(State::C, sm.State());
}

TEST(StateMachineTest, GuardClauseCalledOnlyOnce)
{
    auto sm = MakeSimpleMachine(State::A);
    int callCount = 0;
    sm.Configure(State::A).PermitIf(Trigger::X, State::B, [&]() {
        ++callCount;
        return true;
    });
    sm.Fire(Trigger::X);
    EXPECT_EQ(1, callCount);
}

// ===========================================================================
// Entry / Exit actions
// ===========================================================================

TEST(StateMachineTest, WhenTriggerIsIgnored_ActionsNotExecuted)
{
    auto sm = MakeSimpleMachine(State::B);
    bool fired = false;
    sm.Configure(State::B)
        .OnEntry([&]() { fired = true; })
        .Ignore(Trigger::X);
    sm.Fire(Trigger::X);
    EXPECT_FALSE(fired);
}

TEST(StateMachineTest, IfSelfTransitionPermitted_ActionsFire)
{
    auto sm = MakeSimpleMachine(State::B);
    bool fired = false;
    sm.Configure(State::B)
        .OnEntry([&]() { fired = true; })
        .PermitReentry(Trigger::X);
    sm.Fire(Trigger::X);
    EXPECT_TRUE(fired);
}

TEST(StateMachineTest, IgnoreVsPermitReentry_OnlyReentryFiresEntry)
{
    auto sm = MakeSimpleMachine(State::A);
    int numCalls = 0;
    sm.Configure(State::A)
        .OnEntry([&]() { numCalls++; })
        .PermitReentry(Trigger::X)
        .Ignore(Trigger::Y);

    sm.Fire(Trigger::X);
    sm.Fire(Trigger::Y);
    EXPECT_EQ(1, numCalls);
}

TEST(StateMachineTest, OnExitFiresOnlyOnceReentrySubstate)
{
    auto sm = MakeSimpleMachine(State::A);
    int exitB = 0, exitA = 0, entryB = 0, entryA = 0;

    sm.Configure(State::A)
        .SubstateOf(State::B)
        .OnEntry([&]() { entryA++; })
        .PermitReentry(Trigger::X)
        .OnExit([&]() { exitA++; });

    sm.Configure(State::B)
        .OnEntry([&]() { entryB++; })
        .OnExit([&]() { exitB++; });

    sm.Fire(Trigger::X);

    EXPECT_EQ(0, exitB);
    EXPECT_EQ(0, entryB);
    EXPECT_EQ(1, exitA);
    EXPECT_EQ(1, entryA);
}

TEST(StateMachineTest, TransitionToSuperstateDoesNotExitSuperstate)
{
    auto sm = MakeSimpleMachine(State::B);
    bool superExit = false, superEntry = false, subExit = false;

    sm.Configure(State::A)
        .OnEntry([&]() { superEntry = true; })
        .OnExit([&]() { superExit = true; });

    sm.Configure(State::B)
        .SubstateOf(State::A)
        .Permit(Trigger::Y, State::A)
        .OnExit([&]() { subExit = true; });

    sm.Fire(Trigger::Y);

    EXPECT_TRUE(subExit);
    EXPECT_FALSE(superEntry);
    EXPECT_FALSE(superExit);
}

// ===========================================================================
// OnTransitioned / OnTransitionCompleted
// ===========================================================================

TEST(StateMachineTest, WhenTransitionOccurs_OnTransitionedEventFires)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.Configure(State::B).Permit(Trigger::X, State::A);

    StateMachine<State, Trigger>::Transition* captured = nullptr;
    StateMachine<State, Trigger>::Transition stored{State::B, State::B, Trigger::X};

    sm.OnTransitioned([&](const StateMachine<State, Trigger>::Transition& t) {
        stored = t;
        captured = &stored;
    });

    sm.Fire(Trigger::X);

    ASSERT_NE(nullptr, captured);
    EXPECT_EQ(Trigger::X, stored.trigger);
    EXPECT_EQ(State::B, stored.source);
    EXPECT_EQ(State::A, stored.destination);
}

TEST(StateMachineTest, WhenTransitionOccurs_OnTransitionCompletedEventFires)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.Configure(State::B).Permit(Trigger::X, State::A);

    bool fired = false;
    sm.OnTransitionCompleted([&](const StateMachine<State, Trigger>::Transition& t) {
        EXPECT_EQ(Trigger::X, t.trigger);
        EXPECT_EQ(State::B, t.source);
        EXPECT_EQ(State::A, t.destination);
        fired = true;
    });

    sm.Fire(Trigger::X);
    EXPECT_TRUE(fired);
}

TEST(StateMachineTest, TransitionEventOrderIsCorrect)
{
    // Expected: OnExit -> OnTransitioned -> OnEntry -> OnTransitionCompleted
    auto sm = MakeSimpleMachine(State::B);
    std::vector<std::string> order;

    sm.Configure(State::B)
        .Permit(Trigger::X, State::A)
        .OnExit([&]() { order.emplace_back("OnExit"); });

    sm.Configure(State::A)
        .OnEntry([&]() { order.emplace_back("OnEntry"); });

    sm.OnTransitioned([&](const auto&) { order.emplace_back("OnTransitioned"); });
    sm.OnTransitionCompleted([&](const auto&) { order.emplace_back("OnTransitionCompleted"); });

    sm.Fire(Trigger::X);

    ASSERT_EQ(4u, order.size());
    EXPECT_EQ("OnExit", order[0]);
    EXPECT_EQ("OnTransitioned", order[1]);
    EXPECT_EQ("OnEntry", order[2]);
    EXPECT_EQ("OnTransitionCompleted", order[3]);
}

// ===========================================================================
// Unhandled trigger
// ===========================================================================

TEST(StateMachineTest, ExceptionThrownForInvalidTransition)
{
    auto sm = MakeSimpleMachine(State::A);
    EXPECT_THROW(sm.Fire(Trigger::X), std::logic_error);
}

TEST(StateMachineTest, WhenUnhandledTriggerIsFired_ProvidedHandlerIsCalled)
{
    auto sm = MakeSimpleMachine(State::B);

    State capturedState{};
    Trigger capturedTrigger{};
    sm.OnUnhandledTrigger([&](const State& s, const Trigger& t, const std::vector<std::string>&) {
        capturedState = s;
        capturedTrigger = t;
    });

    sm.Fire(Trigger::Z);

    EXPECT_EQ(State::B, capturedState);
    EXPECT_EQ(Trigger::Z, capturedTrigger);
}

// ===========================================================================
// Implicit reentry disallowed
// ===========================================================================

TEST(StateMachineTest, ImplicitReentryIsDisallowed)
{
    auto sm = MakeSimpleMachine(State::B);
    EXPECT_THROW(sm.Configure(State::B).Permit(Trigger::X, State::B), std::invalid_argument);
}

// ===========================================================================
// Cyclic substate detection
// ===========================================================================

TEST(StateMachineTest, DirectCyclicConfigurationDetected)
{
    auto sm = MakeSimpleMachine(State::A);
    EXPECT_THROW(sm.Configure(State::A).SubstateOf(State::A), std::invalid_argument);
}

TEST(StateMachineTest, NestedCyclicConfigurationDetected)
{
    auto sm = MakeSimpleMachine(State::A);
    sm.Configure(State::B).SubstateOf(State::A);
    EXPECT_THROW(sm.Configure(State::A).SubstateOf(State::B), std::invalid_argument);
}

TEST(StateMachineTest, NestedTwoLevelsCyclicConfigurationDetected)
{
    auto sm = MakeSimpleMachine(State::A);
    sm.Configure(State::B).SubstateOf(State::A);
    sm.Configure(State::C).SubstateOf(State::B);
    EXPECT_THROW(sm.Configure(State::A).SubstateOf(State::C), std::invalid_argument);
}

// ===========================================================================
// CanFire
// ===========================================================================

TEST(StateMachineTest, CanFirePermittedTrigger)
{
    auto sm = MakeSimpleMachine(State::A);
    sm.Configure(State::A).Permit(Trigger::X, State::B);
    EXPECT_TRUE(sm.CanFire(Trigger::X));
}

TEST(StateMachineTest, CanFireReturnsFalseWhenGuardFails)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.Configure(State::B).PermitIf(Trigger::X, State::A, []() { return false; });
    EXPECT_FALSE(sm.CanFire(Trigger::X));
}

TEST(StateMachineTest, CanFire_ReturnsUnmetGuardDescriptions)
{
    const std::string desc = "Guard failed";
    auto sm = MakeSimpleMachine(State::A);
    sm.Configure(State::A).PermitIf(Trigger::X, State::B, []() { return false; }, desc);

    std::vector<std::string> unmet;
    bool result = sm.CanFire(Trigger::X, StateMachine<State, Trigger>::Args{}, unmet);

    EXPECT_FALSE(result);
    ASSERT_EQ(1u, unmet.size());
    EXPECT_EQ(desc, unmet[0]);
}

// ===========================================================================
// Trigger parameters
// ===========================================================================

TEST(StateMachineTest, ParametersPassedToEntryAction)
{
    auto sm = MakeSimpleMachine(State::B);
    auto x = sm.SetTriggerParameters<std::string, int>(Trigger::X);

    sm.Configure(State::B).Permit(Trigger::X, State::C);

    std::string entryArgS;
    int entryArgI = 0;

    sm.Configure(State::C)
        .OnEntryFrom(Trigger::X, [&](const StateMachine<State, Trigger>::Transition& t) {
            entryArgS = std::any_cast<std::string>(t.parameters[0]);
            entryArgI = std::any_cast<int>(t.parameters[1]);
        });

    sm.Fire(x, std::string("something"), 42);

    EXPECT_EQ("something", entryArgS);
    EXPECT_EQ(42, entryArgI);
}

TEST(StateMachineTest, TriggerParametersImmutableOnceSet)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.SetTriggerParameters<std::string, int>(Trigger::X);
    EXPECT_THROW((sm.SetTriggerParameters<std::string>(Trigger::X)), std::logic_error);
}

TEST(StateMachineTest, ParameterizedGuard_TransitionsWhenTrue)
{
    auto sm = MakeSimpleMachine(State::A);
    auto x = sm.SetTriggerParameters<int>(Trigger::X);
    sm.Configure(State::A).PermitIf(Trigger::X, State::B, []() { return true; });
    sm.Fire(x, 2);
    EXPECT_EQ(State::B, sm.State());
}

// ===========================================================================
// Dynamic destination
// ===========================================================================

TEST(StateMachineTest, PermitDynamic_SelectsDestination)
{
    auto sm = MakeSimpleMachine(State::A);
    bool goToB = true;
    sm.Configure(State::A).PermitDynamic(Trigger::X, [&]() { return goToB ? State::B : State::C; });
    sm.Fire(Trigger::X);
    EXPECT_EQ(State::B, sm.State());
}

TEST(StateMachineTest, PermitDynamicIf_SelectsDestinationWhenGuardTrue)
{
    auto sm = MakeSimpleMachine(State::A);
    sm.Configure(State::A)
        .PermitDynamicIf(Trigger::X, []() { return State::B; }, []() { return true; })
        .PermitDynamicIf(Trigger::X, []() { return State::C; }, []() { return false; });
    sm.Fire(Trigger::X);
    EXPECT_EQ(State::B, sm.State());
}

// ===========================================================================
// Internal transitions
// ===========================================================================

TEST(StateMachineTest, InternalTransition_DoesNotChangeState)
{
    auto sm = MakeSimpleMachine(State::A);
    int actionCount = 0;
    sm.Configure(State::A).InternalTransition(Trigger::X, [&]() { actionCount++; });

    sm.Fire(Trigger::X);
    sm.Fire(Trigger::X);

    EXPECT_EQ(State::A, sm.State());
    EXPECT_EQ(2, actionCount);
}

TEST(StateMachineTest, InternalTransition_DoesNotFireEntryOrExit)
{
    auto sm = MakeSimpleMachine(State::A);
    bool entryFired = false, exitFired = false;

    sm.Configure(State::A)
        .OnEntry([&]() { entryFired = true; })
        .OnExit([&]() { exitFired = true; })
        .InternalTransition(Trigger::X, []() {});

    sm.Fire(Trigger::X);

    EXPECT_FALSE(entryFired);
    EXPECT_FALSE(exitFired);
}

// ===========================================================================
// Initial transition
// ===========================================================================

TEST(StateMachineTest, InitialTransition_EntersSubstateOnEntry)
{
    auto sm = MakeSimpleMachine(State::A);

    sm.Configure(State::B)
        .SubstateOf(State::C);

    sm.Configure(State::C)
        .InitialTransition(State::B);

    sm.Configure(State::A)
        .Permit(Trigger::X, State::C);

    sm.Fire(Trigger::X);
    EXPECT_EQ(State::B, sm.State());
}

// ===========================================================================
// Async fire
// ===========================================================================

TEST(StateMachineTest, FireAsync_BasicTransition)
{
    auto sm = MakeSimpleMachine(State::A);
    sm.Configure(State::A).Permit(Trigger::X, State::B);

    auto f = sm.FireAsync(Trigger::X);
    f.get();

    EXPECT_EQ(State::B, sm.State());
}

