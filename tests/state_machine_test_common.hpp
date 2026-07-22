#ifndef STATE_MACHINE_TEST_COMMON_HPP
#define STATE_MACHINE_TEST_COMMON_HPP

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Clownfish/Clownfish.hpp"

using namespace Clownfish;

enum class State { A, B, C };
enum class Trigger { X, Y, Z };

inline StateMachine<State, Trigger> MakeSimpleMachine(const State initial = State::B)
{
    return StateMachine<State, Trigger>(initial);
}

#endif // STATE_MACHINE_TEST_COMMON_HPP

