#include "scenarios.hpp"

#include <any>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Clownfish/Clownfish.hpp"

namespace examples {
namespace {

enum class State {
    A,
    B,
    C,
    D,
};

enum class Trigger {
    X,
    Y,
};

const char* StateName(State state) {
    switch (state) {
        case State::A:
            return "A";
        case State::B:
            return "B";
        case State::C:
            return "C";
        case State::D:
            return "D";
        default:
            return "Unknown";
    }
}

}  // namespace

void RunBasicTransitionExample() {
    std::cout << "\n[BasicTransition]\n";

    Clownfish::StateMachine<State, Trigger> sm(State::A);
    sm.Configure(State::A).Permit(Trigger::X, State::B);

    sm.OnTransitioned([](const auto& transition) {
        std::cout << "  transitioned: " << StateName(transition.source)
                  << " -> " << StateName(transition.destination) << "\n";
    });

    sm.Trigger(Trigger::X);
    std::cout << "  current state: " << StateName(sm.State()) << "\n";
}

void RunFireAsyncExample() {
    std::cout << "\n[FireAsync]\n";

    Clownfish::StateMachine<State, Trigger> sm(State::A);
    sm.Configure(State::A).Permit(Trigger::X, State::B);

    bool transitioned_sync = false;
    bool transitioned_async = false;
    bool completed_async = false;

    sm.OnTransitioned([&](const auto&) {
        transitioned_sync = true;
    });

    sm.OnTransitionedAsync([&](const auto& transition) {
        const auto source = transition.source;
        const auto destination = transition.destination;
        return std::async(std::launch::deferred, [&, source, destination]() {
            transitioned_async = true;
            std::cout << "  transitioned async: " << StateName(source)
                      << " -> " << StateName(destination) << "\n";
        });
    });

    sm.OnTransitionCompletedAsync([&](const auto& transition) {
        const auto source = transition.source;
        const auto destination = transition.destination;
        return std::async(std::launch::deferred, [&, source, destination]() {
            completed_async = true;
            std::cout << "  completed async: " << StateName(source)
                      << " -> " << StateName(destination) << "\n";
        });
    });

    sm.TriggerAsync(Trigger::X).get();

    if (sm.State() != State::B || !transitioned_sync || !transitioned_async || !completed_async) {
        throw std::logic_error("FireAsync example validation failed.");
    }

    std::cout << "  current state: " << StateName(sm.State()) << "\n";
}

void RunGuardAndParametersExample() {
    std::cout << "\n[GuardAndParameters]\n";

    Clownfish::StateMachine<State, Trigger> sm(State::A);
    auto x = sm.SetTriggerParameters<std::string, int>(Trigger::X);

    bool allow = false;
    sm.Configure(State::A).PermitIf(Trigger::X, State::B, [&allow]() { return allow; }, "allow flag must be true");

    std::vector<std::string> unmet;
    if (!sm.CanTrigger(Trigger::X, {}, unmet)) {
        std::cout << "  CanFire(X)=false, unmet guards:";
        for (const auto& g : unmet) {
            std::cout << " [" << g << "]";
        }
        std::cout << "\n";
    }

    std::string receivedText;
    int receivedNumber = 0;
    sm.Configure(State::B).OnEntryFrom(Trigger::X, [&](const auto& transition) {
        receivedText = std::any_cast<std::string>(transition.parameters[0]);
        receivedNumber = std::any_cast<int>(transition.parameters[1]);
    });

    allow = true;
    sm.Trigger(x, std::string("hello"), 42);

    std::cout << "  current state: " << StateName(sm.State()) << "\n";
    std::cout << "  entry parameters: text=" << receivedText << ", number=" << receivedNumber << "\n";
}

void RunHierarchyAndInitialTransitionExample() {
    std::cout << "\n[HierarchyAndInitialTransition]\n";

    Clownfish::StateMachine<State, Trigger> sm(State::A);

    sm.Configure(State::B).SubstateOf(State::C);
    sm.Configure(State::C).InitialTransition(State::B);
    sm.Configure(State::A).Permit(Trigger::X, State::C);
    sm.Configure(State::C).Ignore(Trigger::Y);

    sm.OnTransitionCompleted([](const auto& transition) {
        std::cout << "  completed: " << StateName(transition.source)
                  << " -> " << StateName(transition.destination) << "\n";
    });

    sm.Trigger(Trigger::X);

    std::cout << "  current state: " << StateName(sm.State()) << "\n";
    std::cout << "  IsInState(C): " << (sm.IsInState(State::C) ? "true" : "false") << "\n";
}

}  // namespace examples
