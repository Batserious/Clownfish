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
    Clownfish::StateMachine<State, Trigger> machine;

    machine.Configure(State::On)
    ->Permit(Trigger::TurnOff, State::Off, []()
    {
        std::cout << "Turn on." << std::endl;
    });

    machine.Configure(State::Off)
    ->Permit(Trigger::TurnOn, State::On, []()
    {
        std::cout << "Turn off." << std::endl;
    });

    machine.SetInitialState(State::Off);
    machine.Touch(Trigger::TurnOn);
    machine.Touch(Trigger::TurnOff);

    return 0;
}
