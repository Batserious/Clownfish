#ifndef STATEMACHINE_HPP
#define STATEMACHINE_HPP

#include <functional>
#include <map>
#include <type_traits>
#include <mutex>


template <typename T>
concept Enum = std::is_enum_v<T>;

template <Enum TState, Enum TTrigger>
class StateMachine
{
public:
    StateMachine() = default;
    ~StateMachine() = default;

    void SetInitialState(TState state)
    {
        current = state;
    }

    void Permit(TState source, TTrigger trigger, TState target, std::function<void()> action = nullptr)
    {
        transitions[source][trigger].first = target;
        transitions[source][trigger].second = action;
    }

    void Touch(TTrigger trigger)
    {
        std::lock_guard<std::mutex> locker(mutex);
        auto action = transitions[current][trigger].second;
        current = transitions[current][trigger].first;
        action();
    }

private:
    TState current;

    std::mutex mutex;

    std::map<TState, std::map<TTrigger, std::pair<TState, std::function<void()>>>> transitions;
};


#endif //STATEMACHINE_HPP
