#include "state_machine_test_common.hpp"

TEST(StateMachineNormalTest, InitialStateIsCurrent)
{
    auto sm = MakeSimpleMachine(State::B);
    EXPECT_EQ(State::B, sm.State());
}

TEST(StateMachineNormalTest, CanUseEnumMarkers)
{
    auto sm = MakeSimpleMachine(State::A);
    sm.Configure(State::A).Permit(Trigger::X, State::B);
    sm.Trigger(Trigger::X);
    EXPECT_EQ(State::B, sm.State());
}

TEST(StateMachineNormalTest, StateCanBeStoredExternally)
{
    State state = State::B;
    StateMachine<State, Trigger> sm(
        [&]() { return state; },
        [&](const State& s) { state = s; });
    sm.Configure(State::B).Permit(Trigger::X, State::C);
    EXPECT_EQ(State::B, sm.State());
    EXPECT_EQ(State::B, state);
    sm.Trigger(Trigger::X);
    EXPECT_EQ(State::C, sm.State());
    EXPECT_EQ(State::C, state);
}

TEST(StateMachineNormalTest, StateMutatorCalledOnlyOnce)
{
    State state = State::B;
    int count = 0;
    StateMachine<State, Trigger> sm(
        [&]() { return state; },
        [&](const State& s) { state = s; ++count; });
    sm.Configure(State::B).Permit(Trigger::X, State::C);
    sm.Trigger(Trigger::X);
    EXPECT_EQ(1, count);
}

TEST(StateMachineNormalTest, SubstateIsIncludedInCurrentState)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.Configure(State::B).SubstateOf(State::C);
    EXPECT_EQ(State::B, sm.State());
    EXPECT_TRUE(sm.IsInState(State::C));
}

TEST(StateMachineNormalTest, WhenInSubstate_TriggerIgnoredInSuperstate_RemainsInSubstate)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.Configure(State::B).SubstateOf(State::C);
    sm.Configure(State::C).Ignore(Trigger::X);
    sm.Trigger(Trigger::X);
    EXPECT_EQ(State::B, sm.State());
}

TEST(StateMachineNormalTest, WhenInSubstate_TriggerSuperStateTwice_DoesNotReenterSubstate)
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

    sm.Trigger(Trigger::X);
    sm.Trigger(Trigger::X);

    EXPECT_EQ(1, eCount);
}

TEST(StateMachineNormalTest, PermittedTriggersIncludeSuperstatePermittedTriggers)
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

TEST(StateMachineNormalTest, PermittedTriggersAreDistinct)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.Configure(State::B).SubstateOf(State::C).Permit(Trigger::X, State::A);
    sm.Configure(State::C).Permit(Trigger::X, State::B);

    auto permitted = sm.GetPermittedTriggers();
    int count = static_cast<int>(std::count(permitted.begin(), permitted.end(), Trigger::X));
    EXPECT_EQ(1, count);
}

TEST(StateMachineNormalTest, AcceptedTriggersRespectGuards)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.Configure(State::B).PermitIf(Trigger::X, State::A, []() { return false; });
    auto permitted = sm.GetPermittedTriggers();
    EXPECT_EQ(0u, permitted.size());
}

TEST(StateMachineNormalTest, WhenDiscriminatedByGuard_ChoosesPermittedTransition)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.Configure(State::B)
        .PermitIf(Trigger::X, State::A, []() { return false; })
        .PermitIf(Trigger::X, State::C, []() { return true; });
    sm.Trigger(Trigger::X);
    EXPECT_EQ(State::C, sm.State());
}

TEST(StateMachineNormalTest, GuardClauseCalledOnlyOnce)
{
    auto sm = MakeSimpleMachine(State::A);
    int callCount = 0;
    sm.Configure(State::A).PermitIf(Trigger::X, State::B, [&]() {
        ++callCount;
        return true;
    });
    sm.Trigger(Trigger::X);
    EXPECT_EQ(1, callCount);
}

TEST(StateMachineNormalTest, WhenTriggerIsIgnored_ActionsNotExecuted)
{
    auto sm = MakeSimpleMachine(State::B);
    bool fired = false;
    sm.Configure(State::B)
        .OnEntry([&]() { fired = true; })
        .Ignore(Trigger::X);
    sm.Trigger(Trigger::X);
    EXPECT_FALSE(fired);
}

TEST(StateMachineNormalTest, IfSelfTransitionPermitted_ActionsFire)
{
    auto sm = MakeSimpleMachine(State::B);
    bool fired = false;
    sm.Configure(State::B)
        .OnEntry([&]() { fired = true; })
        .PermitReentry(Trigger::X);
    sm.Trigger(Trigger::X);
    EXPECT_TRUE(fired);
}

TEST(StateMachineNormalTest, IgnoreVsPermitReentry_OnlyReentryFiresEntry)
{
    auto sm = MakeSimpleMachine(State::A);
    int numCalls = 0;
    sm.Configure(State::A)
        .OnEntry([&]() { numCalls++; })
        .PermitReentry(Trigger::X)
        .Ignore(Trigger::Y);

    sm.Trigger(Trigger::X);
    sm.Trigger(Trigger::Y);
    EXPECT_EQ(1, numCalls);
}

TEST(StateMachineNormalTest, OnExitFiresOnlyOnceReentrySubstate)
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

    sm.Trigger(Trigger::X);

    EXPECT_EQ(0, exitB);
    EXPECT_EQ(0, entryB);
    EXPECT_EQ(1, exitA);
    EXPECT_EQ(1, entryA);
}

TEST(StateMachineNormalTest, TransitionToSuperstateDoesNotExitSuperstate)
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

    sm.Trigger(Trigger::Y);

    EXPECT_TRUE(subExit);
    EXPECT_FALSE(superEntry);
    EXPECT_FALSE(superExit);
}

TEST(StateMachineNormalTest, WhenTransitionOccurs_OnTransitionedEventFires)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.Configure(State::B).Permit(Trigger::X, State::A);

    StateMachine<State, Trigger>::Transition* captured = nullptr;
    StateMachine<State, Trigger>::Transition stored{State::B, State::B, Trigger::X};

    sm.OnTransitioned([&](const StateMachine<State, Trigger>::Transition& t) {
        stored = t;
        captured = &stored;
    });

    sm.Trigger(Trigger::X);

    ASSERT_NE(nullptr, captured);
    EXPECT_EQ(Trigger::X, stored.trigger);
    EXPECT_EQ(State::B, stored.source);
    EXPECT_EQ(State::A, stored.destination);
}

TEST(StateMachineNormalTest, WhenTransitionOccurs_OnTransitionCompletedEventFires)
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

    sm.Trigger(Trigger::X);
    EXPECT_TRUE(fired);
}

TEST(StateMachineNormalTest, TransitionEventOrderIsCorrect)
{
    auto sm = MakeSimpleMachine(State::B);
    std::vector<std::string> order;

    sm.Configure(State::B)
        .Permit(Trigger::X, State::A)
        .OnExit([&]() { order.emplace_back("OnExit"); });

    sm.Configure(State::A)
        .OnEntry([&]() { order.emplace_back("OnEntry"); });

    sm.OnTransitioned([&](const auto&) { order.emplace_back("OnTransitioned"); });
    sm.OnTransitionCompleted([&](const auto&) { order.emplace_back("OnTransitionCompleted"); });

    sm.Trigger(Trigger::X);

    ASSERT_EQ(4u, order.size());
    EXPECT_EQ("OnExit", order[0]);
    EXPECT_EQ("OnTransitioned", order[1]);
    EXPECT_EQ("OnEntry", order[2]);
    EXPECT_EQ("OnTransitionCompleted", order[3]);
}

TEST(StateMachineNormalTest, CanFirePermittedTrigger)
{
    auto sm = MakeSimpleMachine(State::A);
    sm.Configure(State::A).Permit(Trigger::X, State::B);
    EXPECT_TRUE(sm.CanTrigger(Trigger::X));
}

TEST(StateMachineNormalTest, CanFireReturnsFalseWhenGuardFails)
{
    auto sm = MakeSimpleMachine(State::B);
    sm.Configure(State::B).PermitIf(Trigger::X, State::A, []() { return false; });
    EXPECT_FALSE(sm.CanTrigger(Trigger::X));
}

TEST(StateMachineNormalTest, CanFire_ReturnsUnmetGuardDescriptions)
{
    const std::string desc = "Guard failed";
    auto sm = MakeSimpleMachine(State::A);
    sm.Configure(State::A).PermitIf(Trigger::X, State::B, []() { return false; }, desc);

    std::vector<std::string> unmet;
    bool result = sm.CanTrigger(Trigger::X, StateMachine<State, Trigger>::Args{}, unmet);

    EXPECT_FALSE(result);
    ASSERT_EQ(1u, unmet.size());
    EXPECT_EQ(desc, unmet[0]);
}

TEST(StateMachineNormalTest, ParametersPassedToEntryAction)
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

    sm.Trigger(x, std::string("something"), 42);

    EXPECT_EQ("something", entryArgS);
    EXPECT_EQ(42, entryArgI);
}

TEST(StateMachineNormalTest, ParameterizedGuard_TransitionsWhenTrue)
{
    auto sm = MakeSimpleMachine(State::A);
    auto x = sm.SetTriggerParameters<int>(Trigger::X);
    sm.Configure(State::A).PermitIf(Trigger::X, State::B, []() { return true; });
    sm.Trigger(x, 2);
    EXPECT_EQ(State::B, sm.State());
}

TEST(StateMachineNormalTest, PermitDynamic_SelectsDestination)
{
    auto sm = MakeSimpleMachine(State::A);
    bool goToB = true;
    sm.Configure(State::A).PermitDynamic(Trigger::X, [&]() { return goToB ? State::B : State::C; });
    sm.Trigger(Trigger::X);
    EXPECT_EQ(State::B, sm.State());
}

TEST(StateMachineNormalTest, PermitDynamicIf_SelectsDestinationWhenGuardTrue)
{
    auto sm = MakeSimpleMachine(State::A);
    sm.Configure(State::A)
        .PermitDynamicIf(Trigger::X, []() { return State::B; }, []() { return true; })
        .PermitDynamicIf(Trigger::X, []() { return State::C; }, []() { return false; });
    sm.Trigger(Trigger::X);
    EXPECT_EQ(State::B, sm.State());
}

TEST(StateMachineNormalTest, InternalTransition_DoesNotChangeState)
{
    auto sm = MakeSimpleMachine(State::A);
    int actionCount = 0;
    sm.Configure(State::A).InternalTransition(Trigger::X, [&]() { actionCount++; });

    sm.Trigger(Trigger::X);
    sm.Trigger(Trigger::X);

    EXPECT_EQ(State::A, sm.State());
    EXPECT_EQ(2, actionCount);
}

TEST(StateMachineNormalTest, InternalTransition_DoesNotFireEntryOrExit)
{
    auto sm = MakeSimpleMachine(State::A);
    bool entryFired = false, exitFired = false;

    sm.Configure(State::A)
        .OnEntry([&]() { entryFired = true; })
        .OnExit([&]() { exitFired = true; })
        .InternalTransition(Trigger::X, []() {});

    sm.Trigger(Trigger::X);

    EXPECT_FALSE(entryFired);
    EXPECT_FALSE(exitFired);
}

TEST(StateMachineNormalTest, InitialTransition_EntersSubstateOnEntry)
{
    auto sm = MakeSimpleMachine(State::A);

    sm.Configure(State::B)
        .SubstateOf(State::C);

    sm.Configure(State::C)
        .InitialTransition(State::B);

    sm.Configure(State::A)
        .Permit(Trigger::X, State::C);

    sm.Trigger(Trigger::X);
    EXPECT_EQ(State::B, sm.State());
}

TEST(StateMachineNormalTest, ActivateDeactivate_FollowsHierarchyOrder)
{
    auto sm = MakeSimpleMachine(State::B);
    std::vector<std::string> events;

    sm.Configure(State::B)
        .SubstateOf(State::C)
        .OnActivate([&]() { events.emplace_back("B.activate"); })
        .OnDeactivate([&]() { events.emplace_back("B.deactivate"); });

    sm.Configure(State::C)
        .OnActivate([&]() { events.emplace_back("C.activate"); })
        .OnDeactivate([&]() { events.emplace_back("C.deactivate"); });

    sm.Activate();
    sm.Deactivate();

    ASSERT_EQ(4u, events.size());
    EXPECT_EQ("C.activate", events[0]);
    EXPECT_EQ("B.activate", events[1]);
    EXPECT_EQ("B.deactivate", events[2]);
    EXPECT_EQ("C.deactivate", events[3]);
}

TEST(StateMachineNormalTest, OnEntryFrom_FiresOnlyForMatchingTrigger)
{
    auto sm = MakeSimpleMachine(State::A);
    int anyEntryCount = 0;
    int fromXCount = 0;

    sm.Configure(State::A).Permit(Trigger::X, State::B);

    sm.Configure(State::B)
        .PermitReentry(Trigger::Y)
        .OnEntry([&]() { anyEntryCount++; })
        .OnEntryFrom(Trigger::X, [&]() { fromXCount++; });

    sm.Trigger(Trigger::X);
    sm.Trigger(Trigger::Y);

    EXPECT_EQ(2, anyEntryCount);
    EXPECT_EQ(1, fromXCount);
}

TEST(StateMachineNormalTest, TriggerAsync_BasicTransition)
{
    auto sm = MakeSimpleMachine(State::A);
    sm.Configure(State::A).Permit(Trigger::X, State::B);

    auto f = sm.TriggerAsync(Trigger::X);
    f.get();

    EXPECT_EQ(State::B, sm.State());
}
