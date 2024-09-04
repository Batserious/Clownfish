#include <iostream>

#include "StateMachine.hpp"

enum class State
{
    On,
    Off
};

enum class Trigger
{
    TurnOn,
    TurnOff
};

int main(int argc, char* argv[])
{
    StateMachine<State, Trigger> machine;

    machine.Permit(State::On, Trigger::TurnOff, State::Off, []()
    {
        std::cout << "Turn off." << std::endl;
    });

    machine.Permit(State::Off, Trigger::TurnOn, State::On, []()
    {
        std::cout << "Turn on." << std::endl;
    });

    machine.SetInitialState(State::Off);
    machine.Touch(Trigger::TurnOn);
    machine.Touch(Trigger::TurnOff);

    return 0;
}
