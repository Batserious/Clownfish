#ifndef STATEMACHINE_HPP
#define STATEMACHINE_HPP

#include <functional>
#include <map>
#include <type_traits>
#include <mutex>


namespace Clownfish
{
    template <typename T>
    concept Enum = std::is_enum_v<T>;

    template <Enum TState, Enum TTrigger>
    class StateMachine
    {
    public:
        StateMachine() = default;
        ~StateMachine() = default;

        class StateConfigHelper : std::enable_shared_from_this<StateConfigHelper>
        {
        public:
            StateConfigHelper() = delete;
            StateConfigHelper(StateMachine* state_machine, TState state) : state_machine(state_machine), state(state)
            {

            }

            ~StateConfigHelper() = default;

            StateConfigHelper* Permit(TTrigger trigger, TState target, std::function<void()> action = nullptr)
            {
                state_machine->transitions[state][trigger].first = target;
                state_machine->transitions[state][trigger].second = action;

                return this;
            }

        private:
            StateMachine* state_machine;
            TState state;
        };

        friend StateConfigHelper;

        void SetInitialState(TState state)
        {
            current = state;
        }

        std::shared_ptr<StateConfigHelper> Configure(TState state)
        {
            StateConfigHelper helper(this, state);
            auto ptr = std::make_shared<StateConfigHelper>(helper);
            return ptr;
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
}


#endif //STATEMACHINE_HPP
