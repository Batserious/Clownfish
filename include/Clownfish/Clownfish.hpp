#ifndef CLOWNFISH_HPP
#define CLOWNFISH_HPP

#include <any>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <queue>
#include <stdexcept>
#include <string>
#include <future>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Clownfish {
    enum class FiringMode {
        Immediate,
        Queued,
    };

    template <typename TState, typename TTrigger>
    class StateMachine {
    public:
        using Args = std::vector<std::any>;

        struct Transition {
            TState source;
            TState destination;
            TTrigger trigger;
            Args parameters;

            Transition(const TState& sourceState,
                                 const TState& destinationState,
                                 const TTrigger& triggerValue,
                                 Args parametersValue = Args{})
                    : source(sourceState),
                        destination(destinationState),
                        trigger(triggerValue),
                        parameters(std::move(parametersValue)) {}

            [[nodiscard]] bool IsReentry() const { return source == destination; }
        };

        struct InitialTransition : public Transition {
            using Transition::Transition;
        };

        class TriggerWithParameters {
        public:
            TriggerWithParameters(TTrigger trigger, std::vector<std::type_index> argumentTypes)
                : trigger_(trigger), argument_types_(std::move(argumentTypes)) {}

            const TTrigger& Trigger() const { return trigger_; }

            void ValidateParameters(const Args& args) const {
                if (args.size() != argument_types_.size()) {
                    throw std::invalid_argument("Trigger parameter count mismatch.");
                }

                for (std::size_t i = 0; i < args.size(); ++i) {
                    if (!args[i].has_value()) {
                        continue;
                    }

                    if (std::type_index(args[i].type()) != argument_types_[i]) {
                        throw std::invalid_argument("Trigger parameter type mismatch.");
                    }
                }
            }

        private:
            TTrigger trigger_;
            std::vector<std::type_index> argument_types_;
        };

        template <typename... TArgs>
        class TriggerWithParametersT : public TriggerWithParameters {
        public:
            explicit TriggerWithParametersT(TTrigger trigger)
                : TriggerWithParameters(trigger, std::vector<std::type_index>{std::type_index(typeid(TArgs))...}) {}
        };

    private:
        struct GuardCondition {
            std::function<bool(const Args&)> guard;
            std::string description;
        };

        class TransitionGuard {
        public:
            TransitionGuard() = default;

            explicit TransitionGuard(std::function<bool(const Args&)> guard, std::string description = "") {
                conditions_.push_back(GuardCondition{std::move(guard), std::move(description)});
            }

            [[nodiscard]] bool GuardConditionsMet(const Args& args) const {
                for (const auto& c : conditions_) {
                    if (c.guard && !c.guard(args)) {
                        return false;
                    }
                }
                return true;
            }

            [[nodiscard]] std::vector<std::string> UnmetGuardConditions(const Args& args) const {
                std::vector<std::string> unmet;
                for (const auto& c : conditions_) {
                    if (c.guard && !c.guard(args)) {
                        unmet.push_back(c.description.empty() ? "Guard condition unmet" : c.description);
                    }
                }
                return unmet;
            }

        private:
            std::vector<GuardCondition> conditions_;
        };

        enum class BehaviourKind {
            Transitioning,
            Reentry,
            Dynamic,
            Ignored,
            Internal,
        };

        class TriggerBehaviour {
        public:
            TriggerBehaviour(TTrigger trigger, TransitionGuard guard)
                : trigger_(std::move(trigger)), guard_(std::move(guard)) {}
            virtual ~TriggerBehaviour() = default;

            const TTrigger& Trigger() const { return trigger_; }
            [[nodiscard]] std::vector<std::string> UnmetGuardConditions(const Args& args) const { return guard_.UnmetGuardConditions(args); }
            [[nodiscard]] bool GuardConditionsMet(const Args& args) const { return guard_.GuardConditionsMet(args); }

            virtual BehaviourKind Kind() const = 0;

        private:
            TTrigger trigger_;
            TransitionGuard guard_;
        };

        class TransitioningTriggerBehaviour : public TriggerBehaviour {
        public:
            TransitioningTriggerBehaviour(TTrigger trigger, TState destination, TransitionGuard guard)
                : TriggerBehaviour(std::move(trigger), std::move(guard)), destination_(std::move(destination)) {}

            BehaviourKind Kind() const override { return BehaviourKind::Transitioning; }
            const TState& Destination() const { return destination_; }

        private:
            TState destination_;
        };

        class ReentryTriggerBehaviour : public TriggerBehaviour {
        public:
            ReentryTriggerBehaviour(TTrigger trigger, TState destination, TransitionGuard guard)
                : TriggerBehaviour(std::move(trigger), std::move(guard)), destination_(std::move(destination)) {}

            BehaviourKind Kind() const override { return BehaviourKind::Reentry; }
            const TState& Destination() const { return destination_; }

        private:
            TState destination_;
        };

        class DynamicTriggerBehaviour : public TriggerBehaviour {
        public:
            DynamicTriggerBehaviour(TTrigger trigger, std::function<TState(const Args&)> selector, TransitionGuard guard)
                : TriggerBehaviour(std::move(trigger), std::move(guard)), selector_(std::move(selector)) {}

            BehaviourKind Kind() const override { return BehaviourKind::Dynamic; }

            TState GetDestinationState(const Args& args) const { return selector_(args); }

        private:
            std::function<TState(const Args&)> selector_;
        };

        class IgnoredTriggerBehaviour : public TriggerBehaviour {
        public:
            IgnoredTriggerBehaviour(TTrigger trigger, TransitionGuard guard)
                : TriggerBehaviour(std::move(trigger), std::move(guard)) {}

            BehaviourKind Kind() const override { return BehaviourKind::Ignored; }
        };

        class InternalTriggerBehaviour : public TriggerBehaviour {
        public:
            InternalTriggerBehaviour(TTrigger trigger, std::function<void(const Transition&, const Args&)> action, TransitionGuard guard)
                : TriggerBehaviour(std::move(trigger), std::move(guard)), action_(std::move(action)) {}

            BehaviourKind Kind() const override { return BehaviourKind::Internal; }

            void Execute(const Transition& transition, const Args& args) const { action_(transition, args); }

        private:
            std::function<void(const Transition&, const Args&)> action_;
        };

        struct TriggerBehaviourResult {
            TriggerBehaviour* handler = nullptr;
            std::vector<std::string> unmet_guard_conditions;
        };

        struct EntryAction {
            bool has_trigger_filter = false;
            TTrigger trigger_filter{};
            std::function<void(const Transition&, const Args&)> action;
        };

        struct AsyncEntryAction {
            bool has_trigger_filter = false;
            TTrigger trigger_filter{};
            std::function<std::future<void>(const Transition&, const Args&)> action;
        };

        // Helper: chain a sequence of futures in serial on a background thread.
        static std::future<void> ChainFutures(std::vector<std::future<void>> futures) {
            return std::async(std::launch::async, [futures = std::move(futures)]() mutable {
                for (auto& f : futures) {
                    f.get();
                }
            });
        }

        // Helper: create an already-resolved future.
        static std::future<void> ReadyFuture() {
            std::promise<void> p;
            p.set_value();
            return p.get_future();
        }

        struct StateRepresentation {
            explicit StateRepresentation(TState state) : state(std::move(state)) {}

            TState state;
            std::unordered_map<TTrigger, std::vector<std::unique_ptr<TriggerBehaviour>>> trigger_behaviours;
            std::vector<EntryAction> entry_actions;
            std::vector<std::function<void(const Transition&)>> exit_actions;
            std::vector<std::function<void()>> activate_actions;
            std::vector<std::function<void()>> deactivate_actions;

            // Async action collections
            std::vector<AsyncEntryAction> async_entry_actions;
            std::vector<std::function<std::future<void>(const Transition&)>> async_exit_actions;
            std::vector<std::function<std::future<void>()>> async_activate_actions;
            std::vector<std::function<std::future<void>()>> async_deactivate_actions;

            StateRepresentation* superstate = nullptr;
            std::vector<StateRepresentation*> substates;

            bool has_initial_transition = false;
            TState initial_transition_target{};

            bool Includes(const TState& candidate) const {
                if (state == candidate) {
                    return true;
                }

                for (const auto* sub : substates) {
                    if (sub->Includes(candidate)) {
                        return true;
                    }
                }

                return false;
            }

            bool IsIncludedIn(const TState& candidate) const {
                if (state == candidate) {
                    return true;
                }

                return superstate != nullptr && superstate->IsIncludedIn(candidate);
            }

            void AddTriggerBehaviour(std::unique_ptr<TriggerBehaviour> behaviour) {
                auto trigger = behaviour->Trigger();
                trigger_behaviours[trigger].push_back(std::move(behaviour));
            }

            void AddEntryAction(std::function<void(const Transition&, const Args&)> action) {
                entry_actions.push_back(EntryAction{false, TTrigger{}, std::move(action)});
            }

            void AddEntryActionFrom(TTrigger trigger, std::function<void(const Transition&, const Args&)> action) {
                entry_actions.push_back(EntryAction{true, std::move(trigger), std::move(action)});
            }

            void AddExitAction(std::function<void(const Transition&)> action) {
                exit_actions.push_back(std::move(action));
            }

            void AddActivateAction(std::function<void()> action) {
                activate_actions.push_back(std::move(action));
            }

            void AddDeactivateAction(std::function<void()> action) {
                deactivate_actions.push_back(std::move(action));
            }

            void AddAsyncEntryAction(std::function<std::future<void>(const Transition&, const Args&)> action) {
                async_entry_actions.push_back(AsyncEntryAction{false, TTrigger{}, std::move(action)});
            }

            void AddAsyncEntryActionFrom(TTrigger trigger, std::function<std::future<void>(const Transition&, const Args&)> action) {
                async_entry_actions.push_back(AsyncEntryAction{true, std::move(trigger), std::move(action)});
            }

            void AddAsyncExitAction(std::function<std::future<void>(const Transition&)> action) {
                async_exit_actions.push_back(std::move(action));
            }

            void AddAsyncActivateAction(std::function<std::future<void>()> action) {
                async_activate_actions.push_back(std::move(action));
            }

            void AddAsyncDeactivateAction(std::function<std::future<void>()> action) {
                async_deactivate_actions.push_back(std::move(action));
            }

            bool TryFindLocalHandler(const TTrigger& trigger, const Args& args, TriggerBehaviourResult& result) {
                auto it = trigger_behaviours.find(trigger);
                if (it == trigger_behaviours.end()) {
                    return false;
                }

                std::vector<TriggerBehaviourResult> candidates;
                candidates.reserve(it->second.size());
                for (auto& behaviour : it->second) {
                    candidates.push_back(TriggerBehaviourResult{behaviour.get(), behaviour->UnmetGuardConditions(args)});
                }

                TriggerBehaviourResult* success = nullptr;
                for (auto& c : candidates) {
                    if (c.unmet_guard_conditions.empty()) {
                        if (success != nullptr) {
                            throw std::logic_error("Multiple transitions are permitted for the same trigger and state.");
                        }
                        success = &c;
                    }
                }

                if (success != nullptr) {
                    result = *success;
                    return true;
                }

                if (!candidates.empty()) {
                    result = candidates.front();
                    for (std::size_t i = 1; i < candidates.size(); ++i) {
                        for (const auto& unmet : candidates[i].unmet_guard_conditions) {
                            bool exists = false;
                            for (const auto& recorded : result.unmet_guard_conditions) {
                                if (recorded == unmet) {
                                    exists = true;
                                    break;
                                }
                            }

                            if (!exists) {
                                result.unmet_guard_conditions.push_back(unmet);
                            }
                        }
                    }
                    return true;
                }

                return false;
            }

            bool TryFindHandler(const TTrigger& trigger, const Args& args, TriggerBehaviourResult& result) {
                TriggerBehaviourResult local;
                const bool has_local = TryFindLocalHandler(trigger, args, local);

                TriggerBehaviourResult from_super;
                const bool has_super = superstate != nullptr && superstate->TryFindHandler(trigger, args, from_super);

                if (!has_local && !has_super) {
                    return false;
                }

                result = has_super ? from_super : local;
                return true;
            }

            void ExecuteEntryActions(const Transition& transition, const Args& args) {
                for (const auto& action : entry_actions) {
                    if (!action.has_trigger_filter || action.trigger_filter == transition.trigger) {
                        action.action(transition, args);
                    }
                }
            }

            void ExecuteExitActions(const Transition& transition) {
                for (const auto& action : exit_actions) {
                    action(transition);
                }
            }

            void Enter(const Transition& transition, const Args& args) {
                if (transition.IsReentry()) {
                    ExecuteEntryActions(transition, args);
                    return;
                }

                if (!Includes(transition.source)) {
                    if (superstate != nullptr) {
                        superstate->Enter(transition, args);
                    }
                    ExecuteEntryActions(transition, args);
                }
            }

            Transition Exit(const Transition& transition) {
                if (transition.IsReentry()) {
                    ExecuteExitActions(transition);
                    return transition;
                }

                if (!Includes(transition.destination)) {
                    ExecuteExitActions(transition);

                    if (superstate != nullptr) {
                        if (IsIncludedIn(transition.destination)) {
                            if (superstate->state != transition.destination) {
                                return superstate->Exit(transition);
                            }
                        } else {
                            return superstate->Exit(transition);
                        }
                    }
                }

                return transition;
            }

            void Activate() {
                if (superstate != nullptr) {
                    superstate->Activate();
                }
                for (const auto& action : activate_actions) {
                    action();
                }
            }

            void Deactivate() {
                for (const auto& action : deactivate_actions) {
                    action();
                }
                if (superstate != nullptr) {
                    superstate->Deactivate();
                }
            }

            // ── Async variants ──────────────────────────────────────────────────

            std::future<void> ExecuteEntryActionsAsync(const Transition& transition, const Args& args) {
                // Run sync actions immediately, then chain async ones.
                ExecuteEntryActions(transition, args);
                std::vector<std::future<void>> futures;
                for (auto& a : async_entry_actions) {
                    if (!a.has_trigger_filter || a.trigger_filter == transition.trigger) {
                        futures.push_back(a.action(transition, args));
                    }
                }
                if (futures.empty()) return ReadyFuture();
                return ChainFutures(std::move(futures));
            }

            std::future<void> ExecuteExitActionsAsync(const Transition& transition) {
                ExecuteExitActions(transition);
                std::vector<std::future<void>> futures;
                for (auto& a : async_exit_actions) {
                    futures.push_back(a(transition));
                }
                if (futures.empty()) return ReadyFuture();
                return ChainFutures(std::move(futures));
            }

            std::future<void> EnterAsync(const Transition& transition, const Args& args) {
                if (transition.IsReentry()) {
                    return ExecuteEntryActionsAsync(transition, args);
                }
                if (!Includes(transition.source)) {
                    if (superstate != nullptr) {
                        superstate->EnterAsync(transition, args).get();
                    }
                    return ExecuteEntryActionsAsync(transition, args);
                }
                return ReadyFuture();
            }

            std::future<void> ExitAsync(const Transition& transition) {
                if (transition.IsReentry()) {
                    return ExecuteExitActionsAsync(transition);
                }
                if (!Includes(transition.destination)) {
                    auto f = ExecuteExitActionsAsync(transition);
                    f.get();
                    if (superstate != nullptr) {
                        if (IsIncludedIn(transition.destination)) {
                            if (superstate->state != transition.destination) {
                                return superstate->ExitAsync(transition);
                            }
                        } else {
                            return superstate->ExitAsync(transition);
                        }
                    }
                }
                return ReadyFuture();
            }

            std::future<void> ActivateAsync() {
                std::future<void> superFut = superstate != nullptr ? superstate->ActivateAsync() : ReadyFuture();
                return std::async(std::launch::async, [sf = std::move(superFut), this]() mutable {
                    sf.get();
                    Activate();  // sync activate actions
                    std::vector<std::future<void>> futures;
                    futures.reserve(async_activate_actions.size());
                    for (auto& a : async_activate_actions) {
                        futures.push_back(a());
                    }
                    ChainFutures(std::move(futures)).get();
                });
            }

            std::future<void> DeactivateAsync() {
                return std::async(std::launch::async, [this]() mutable {
                    for (auto& a : deactivate_actions) a();
                    std::vector<std::future<void>> futures;
                    futures.reserve(async_deactivate_actions.size());
                    for (auto& a : async_deactivate_actions) {
                        futures.push_back(a());
                    }
                    ChainFutures(std::move(futures)).get();
                    if (superstate != nullptr) {
                        superstate->DeactivateAsync().get();
                    }
                });
            }
        };

    public:
        class StateConfiguration {
        public:
            StateConfiguration(StateMachine* machine, StateRepresentation* representation)
                : machine_(machine), representation_(representation) {}

            StateConfiguration& Permit(const TTrigger& trigger, const TState& destinationState) {
                EnforceNotIdentityTransition(destinationState);
                representation_->AddTriggerBehaviour(std::make_unique<TransitioningTriggerBehaviour>(trigger, destinationState, TransitionGuard{}));
                return *this;
            }

            StateConfiguration& PermitIf(const TTrigger& trigger, const TState& destinationState, std::function<bool()> guard, const std::string& guardDescription = "") {
                EnforceNotIdentityTransition(destinationState);
                auto packed = [g = std::move(guard)](const Args&) { return g(); };
                representation_->AddTriggerBehaviour(
                    std::make_unique<TransitioningTriggerBehaviour>(trigger, destinationState, TransitionGuard(std::move(packed), guardDescription)));
                return *this;
            }

            StateConfiguration& PermitReentry(const TTrigger& trigger) {
                representation_->AddTriggerBehaviour(std::make_unique<ReentryTriggerBehaviour>(trigger, representation_->state, TransitionGuard{}));
                return *this;
            }

            StateConfiguration& PermitReentryIf(const TTrigger& trigger, std::function<bool()> guard, const std::string& guardDescription = "") {
                auto packed = [g = std::move(guard)](const Args&) { return g(); };
                representation_->AddTriggerBehaviour(
                    std::make_unique<ReentryTriggerBehaviour>(trigger, representation_->state, TransitionGuard(std::move(packed), guardDescription)));
                return *this;
            }

            StateConfiguration& PermitDynamic(const TTrigger& trigger, std::function<TState()> destinationSelector) {
                auto packed = [selector = std::move(destinationSelector)](const Args&) { return selector(); };
                representation_->AddTriggerBehaviour(
                    std::make_unique<DynamicTriggerBehaviour>(trigger, std::move(packed), TransitionGuard{}));
                return *this;
            }

            StateConfiguration& PermitDynamicIf(const TTrigger& trigger,
                                                std::function<TState()> destinationSelector,
                                                std::function<bool()> guard,
                                                const std::string& guardDescription = "") {
                auto packed_destination = [selector = std::move(destinationSelector)](const Args&) { return selector(); };
                auto packed_guard = [g = std::move(guard)](const Args&) { return g(); };
                representation_->AddTriggerBehaviour(
                    std::make_unique<DynamicTriggerBehaviour>(trigger, std::move(packed_destination), TransitionGuard(std::move(packed_guard), guardDescription)));
                return *this;
            }

            StateConfiguration& Ignore(const TTrigger& trigger) {
                representation_->AddTriggerBehaviour(std::make_unique<IgnoredTriggerBehaviour>(trigger, TransitionGuard{}));
                return *this;
            }

            StateConfiguration& IgnoreIf(const TTrigger& trigger, std::function<bool()> guard, const std::string& guardDescription = "") {
                auto packed = [g = std::move(guard)](const Args&) { return g(); };
                representation_->AddTriggerBehaviour(
                    std::make_unique<IgnoredTriggerBehaviour>(trigger, TransitionGuard(std::move(packed), guardDescription)));
                return *this;
            }

            StateConfiguration& InternalTransition(const TTrigger& trigger, std::function<void()> action) {
                auto wrapped = [a = std::move(action)](const Transition&, const Args&) { a(); };
                representation_->AddTriggerBehaviour(
                    std::make_unique<InternalTriggerBehaviour>(trigger, std::move(wrapped), TransitionGuard{}));
                return *this;
            }

            StateConfiguration& InternalTransition(const TTrigger& trigger, std::function<void(const Transition&)> action) {
                auto wrapped = [a = std::move(action)](const Transition& t, const Args&) { a(t); };
                representation_->AddTriggerBehaviour(
                    std::make_unique<InternalTriggerBehaviour>(trigger, std::move(wrapped), TransitionGuard{}));
                return *this;
            }

            StateConfiguration& InternalTransitionIf(const TTrigger& trigger,
                                                     std::function<bool()> guard,
                                                     std::function<void(const Transition&)> action,
                                                     const std::string& guardDescription = "") {
                auto wrapped = [a = std::move(action)](const Transition& t, const Args&) { a(t); };
                auto packed_guard = [g = std::move(guard)](const Args&) { return g(); };
                representation_->AddTriggerBehaviour(
                    std::make_unique<InternalTriggerBehaviour>(trigger, std::move(wrapped), TransitionGuard(std::move(packed_guard), guardDescription)));
                return *this;
            }

            StateConfiguration& OnEntry(std::function<void()> entryAction) {
                representation_->AddEntryAction([a = std::move(entryAction)](const Transition&, const Args&) { a(); });
                return *this;
            }

            StateConfiguration& OnEntry(std::function<void(const Transition&)> entryAction) {
                representation_->AddEntryAction([a = std::move(entryAction)](const Transition& t, const Args&) { a(t); });
                return *this;
            }

            StateConfiguration& OnEntryFrom(const TTrigger& trigger, std::function<void()> entryAction) {
                representation_->AddEntryActionFrom(trigger, [a = std::move(entryAction)](const Transition&, const Args&) { a(); });
                return *this;
            }

            StateConfiguration& OnEntryFrom(const TTrigger& trigger, std::function<void(const Transition&)> entryAction) {
                representation_->AddEntryActionFrom(trigger, [a = std::move(entryAction)](const Transition& t, const Args&) { a(t); });
                return *this;
            }

            StateConfiguration& OnExit(std::function<void()> exitAction) {
                representation_->AddExitAction([a = std::move(exitAction)](const Transition&) { a(); });
                return *this;
            }

            StateConfiguration& OnExit(std::function<void(const Transition&)> exitAction) {
                representation_->AddExitAction(std::move(exitAction));
                return *this;
            }

            StateConfiguration& OnActivate(std::function<void()> activateAction) {
                representation_->AddActivateAction(std::move(activateAction));
                return *this;
            }

            StateConfiguration& OnDeactivate(std::function<void()> deactivateAction) {
                representation_->AddDeactivateAction(std::move(deactivateAction));
                return *this;
            }

            // ── Async entry/exit/activate/deactivate ────────────────────────────

            StateConfiguration& OnEntryAsync(std::function<std::future<void>()> entryAction) {
                representation_->AddAsyncEntryAction(
                    [a = std::move(entryAction)](const Transition&, const Args&) { return a(); });
                return *this;
            }

            StateConfiguration& OnEntryAsync(std::function<std::future<void>(const Transition&)> entryAction) {
                representation_->AddAsyncEntryAction(
                    [a = std::move(entryAction)](const Transition& t, const Args&) { return a(t); });
                return *this;
            }

            StateConfiguration& OnEntryFromAsync(const TTrigger& trigger, std::function<std::future<void>()> entryAction) {
                representation_->AddAsyncEntryActionFrom(
                    trigger, [a = std::move(entryAction)](const Transition&, const Args&) { return a(); });
                return *this;
            }

            StateConfiguration& OnEntryFromAsync(const TTrigger& trigger,
                                                  std::function<std::future<void>(const Transition&)> entryAction) {
                representation_->AddAsyncEntryActionFrom(
                    trigger, [a = std::move(entryAction)](const Transition& t, const Args&) { return a(t); });
                return *this;
            }

            StateConfiguration& OnExitAsync(std::function<std::future<void>()> exitAction) {
                representation_->AddAsyncExitAction([a = std::move(exitAction)](const Transition&) { return a(); });
                return *this;
            }

            StateConfiguration& OnExitAsync(std::function<std::future<void>(const Transition&)> exitAction) {
                representation_->AddAsyncExitAction(std::move(exitAction));
                return *this;
            }

            StateConfiguration& OnActivateAsync(std::function<std::future<void>()> activateAction) {
                representation_->AddAsyncActivateAction(std::move(activateAction));
                return *this;
            }

            StateConfiguration& OnDeactivateAsync(std::function<std::future<void>()> deactivateAction) {
                representation_->AddAsyncDeactivateAction(std::move(deactivateAction));
                return *this;
            }

            StateConfiguration& SubstateOf(const TState& superstate) {
                if (representation_->state == superstate) {
                    throw std::invalid_argument("Configuring a state as a substate of itself creates an illegal cyclic configuration.");
                }

                auto& super_rep = machine_->GetRepresentation(superstate);
                auto* p = &super_rep;
                while (p != nullptr) {
                    if (p->state == representation_->state) {
                        throw std::invalid_argument("Configuring this substate creates an illegal nested cyclic configuration.");
                    }
                    p = p->superstate;
                }

                representation_->superstate = &super_rep;
                super_rep.substates.push_back(representation_);
                return *this;
            }

            StateConfiguration& InitialTransition(const TState& targetState) {
                if (representation_->has_initial_transition) {
                    throw std::logic_error("Initial transition is already configured for this state.");
                }
                if (representation_->state == targetState) {
                    throw std::invalid_argument("Initial transition target cannot be the same as current state.");
                }

                representation_->has_initial_transition = true;
                representation_->initial_transition_target = targetState;
                return *this;
            }

        private:
            void EnforceNotIdentityTransition(const TState& destinationState) {
                if (representation_->state == destinationState) {
                    throw std::invalid_argument("Permit/PermitIf destination must differ from source. Use Ignore or PermitReentry instead.");
                }
            }

            StateMachine* machine_;
            StateRepresentation* representation_;
        };

        explicit StateMachine(const TState& initialState, FiringMode firingMode = FiringMode::Queued)
            : firing_mode_(firingMode),
              state_accessor_([this]() { return this->state_storage_; }),
              state_mutator_([this](const TState& s) { this->state_storage_ = s; }),
              state_storage_(initialState),
              initial_state_(initialState) {
            unhandled_trigger_action_ = [this](const TState& state, const TTrigger& trigger, const std::vector<std::string>& unmet) {
                this->DefaultUnhandledTriggerAction(state, trigger, unmet);
            };
        }

        StateMachine(std::function<TState()> stateAccessor,
                     std::function<void(const TState&)> stateMutator,
                     FiringMode firingMode = FiringMode::Queued)
            : firing_mode_(firingMode),
              state_accessor_(std::move(stateAccessor)),
              state_mutator_(std::move(stateMutator)),
              initial_state_(state_accessor_()) {
            if (!state_accessor_ || !state_mutator_) {
                throw std::invalid_argument("State accessor and mutator must be set.");
            }

            unhandled_trigger_action_ = [this](const TState& state, const TTrigger& trigger, const std::vector<std::string>& unmet) {
                this->DefaultUnhandledTriggerAction(state, trigger, unmet);
            };
        }

        TState State() const { return state_accessor_(); }

        StateConfiguration Configure(const TState& state) {
            return StateConfiguration(this, &GetRepresentation(state));
        }

        template <typename... TArgs>
        TriggerWithParametersT<TArgs...> SetTriggerParameters(const TTrigger& trigger) {
            TriggerWithParametersT<TArgs...> configured(trigger);
            SaveTriggerConfiguration(configured);
            return configured;
        }

        void Fire(const TTrigger& trigger) {
            InternalFire(trigger, Args{});
        }

        void Fire(const TTrigger& trigger, const Args& args) {
            InternalFire(trigger, args);
        }

        template <typename... TArgs>
        void Fire(const TriggerWithParametersT<TArgs...>& trigger, TArgs... args) {
            Args packed;
            packed.reserve(sizeof...(TArgs));
            (packed.emplace_back(std::move(args)), ...);
            trigger.ValidateParameters(packed);
            InternalFire(trigger.Trigger(), packed);
        }

        void Fire(const TriggerWithParameters& trigger, const Args& args) {
            trigger.ValidateParameters(args);
            InternalFire(trigger.Trigger(), args);
        }

        bool CanFire(const TTrigger& trigger) {
            TriggerBehaviourResult result;
            auto& rep = CurrentRepresentation();
            return rep.TryFindHandler(trigger, Args{}, result) && result.unmet_guard_conditions.empty();
        }

        bool CanFire(const TTrigger& trigger, const Args& args, std::vector<std::string>& unmetGuards) {
            TriggerBehaviourResult result;
            auto& rep = CurrentRepresentation();
            if (!rep.TryFindHandler(trigger, args, result)) {
                return false;
            }

            unmetGuards = result.unmet_guard_conditions;
            return result.unmet_guard_conditions.empty();
        }

        std::vector<TTrigger> GetPermittedTriggers(const Args& args = Args{}) {
            std::vector<TTrigger> triggers;
            auto& rep = CurrentRepresentation();

            for (const auto& pair : rep.trigger_behaviours) {
                TriggerBehaviourResult r;
                if (rep.TryFindHandler(pair.first, args, r) && r.unmet_guard_conditions.empty()) {
                    triggers.push_back(pair.first);
                }
            }

            if (rep.superstate != nullptr) {
                for (const auto& pair : rep.superstate->trigger_behaviours) {
                    TriggerBehaviourResult r;
                    if (rep.TryFindHandler(pair.first, args, r) && r.unmet_guard_conditions.empty()) {
                        bool exists = false;
                        for (const auto& trigger : triggers) {
                            if (trigger == pair.first) {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists) {
                            triggers.push_back(pair.first);
                        }
                    }
                }
            }

            return triggers;
        }

        bool IsInState(const TState& state) {
            return CurrentRepresentation().IsIncludedIn(state);
        }

        void Activate() {
            CurrentRepresentation().Activate();
        }

        void Deactivate() {
            CurrentRepresentation().Deactivate();
        }

        void OnUnhandledTrigger(std::function<void(const TState&, const TTrigger&, const std::vector<std::string>&)> handler) {
            unhandled_trigger_action_ = std::move(handler);
        }

        void OnTransitioned(std::function<void(const Transition&)> callback) {
            on_transitioned_.push_back(std::move(callback));
        }

        void OnTransitionCompleted(std::function<void(const Transition&)> callback) {
            on_transition_completed_.push_back(std::move(callback));
        }

        // Async callback registration (invoked after sync callbacks in FireAsync).
        void OnTransitionedAsync(std::function<std::future<void>(const Transition&)> callback) {
            on_transitioned_async_.push_back(std::move(callback));
        }

        void OnTransitionCompletedAsync(std::function<std::future<void>(const Transition&)> callback) {
            on_transition_completed_async_.push_back(std::move(callback));
        }

        void UnregisterAllCallbacks() {
            on_transitioned_.clear();
            on_transition_completed_.clear();
            on_transitioned_async_.clear();
            on_transition_completed_async_.clear();
        }

        // ── Async public API ──────────────────────────────────────────────────

        std::future<void> FireAsync(const TTrigger& trigger) {
            return InternalFireAsync(trigger, Args{});
        }

        std::future<void> FireAsync(const TTrigger& trigger, const Args& args) {
            return InternalFireAsync(trigger, args);
        }

        template <typename... TArgs>
        std::future<void> FireAsync(const TriggerWithParametersT<TArgs...>& trigger, TArgs... args) {
            Args packed;
            packed.reserve(sizeof...(TArgs));
            (packed.emplace_back(std::move(args)), ...);
            trigger.ValidateParameters(packed);
            return InternalFireAsync(trigger.Trigger(), packed);
        }

        std::future<void> ActivateAsync() {
            return CurrentRepresentation().ActivateAsync();
        }

        std::future<void> DeactivateAsync() {
            return CurrentRepresentation().DeactivateAsync();
        }

    private:
        struct QueuedTrigger {
            TTrigger trigger;
            Args args;
        };

        void SaveTriggerConfiguration(const TriggerWithParameters& trigger) {
            auto inserted = trigger_configuration_.emplace(trigger.Trigger(), trigger);
            if (!inserted.second) {
                throw std::logic_error("Cannot reconfigure trigger parameters once configured.");
            }
        }

        StateRepresentation& GetRepresentation(const TState& state) {
            auto it = state_configuration_.find(state);
            if (it == state_configuration_.end()) {
                auto inserted = state_configuration_.emplace(state, std::make_unique<StateRepresentation>(state));
                return *inserted.first->second;
            }
            return *it->second;
        }

        StateRepresentation& CurrentRepresentation() {
            return GetRepresentation(State());
        }

        void SetState(const TState& state) {
            state_mutator_(state);
        }

        void InternalFire(const TTrigger& trigger, const Args& args) {
            switch (firing_mode_) {
                case FiringMode::Immediate:
                    InternalFireOne(trigger, args);
                    return;
                case FiringMode::Queued:
                    InternalFireQueued(trigger, args);
                    return;
                default:
                    throw std::logic_error("Unknown firing mode.");
            }
        }

        void InternalFireQueued(const TTrigger& trigger, const Args& args) {
            event_queue_.push(QueuedTrigger{trigger, args});

            if (firing_) {
                return;
            }

            firing_ = true;
            try {
                while (!event_queue_.empty()) {
                    auto queued = event_queue_.front();
                    event_queue_.pop();
                    InternalFireOne(queued.trigger, queued.args);
                }
            } catch (...) {
                firing_ = false;
                throw;
            }

            firing_ = false;
        }

        void InternalFireOne(const TTrigger& trigger, const Args& args) {
            auto configured = trigger_configuration_.find(trigger);
            if (configured != trigger_configuration_.end()) {
                configured->second.ValidateParameters(args);
            }

            TState source = State();
            auto& rep = GetRepresentation(source);

            TriggerBehaviourResult result;
            if (!rep.TryFindHandler(trigger, args, result)) {
                unhandled_trigger_action_(rep.state, trigger, result.unmet_guard_conditions);
                return;
            }

            if (!result.unmet_guard_conditions.empty()) {
                unhandled_trigger_action_(rep.state, trigger, result.unmet_guard_conditions);
                return;
            }

            if (result.handler == nullptr) {
                throw std::logic_error("State machine configuration is invalid: null trigger handler.");
            }

            switch (result.handler->Kind()) {
                case BehaviourKind::Ignored:
                    return;
                case BehaviourKind::Internal: {
                    auto* internal = static_cast<InternalTriggerBehaviour*>(result.handler);
                    Transition transition{source, source, trigger, args};
                    internal->Execute(transition, args);
                    return;
                }
                case BehaviourKind::Reentry: {
                    auto* reentry = static_cast<ReentryTriggerBehaviour*>(result.handler);
                    Transition transition{source, reentry->Destination(), trigger, args};
                    HandleReentryTrigger(args, rep, transition);
                    return;
                }
                case BehaviourKind::Dynamic: {
                    auto* dynamic = static_cast<DynamicTriggerBehaviour*>(result.handler);
                    const auto destination = dynamic->GetDestinationState(args);
                    Transition transition{source, destination, trigger, args};
                    HandleTransitioningTrigger(args, rep, transition);
                    return;
                }
                case BehaviourKind::Transitioning: {
                    auto* transitioning = static_cast<TransitioningTriggerBehaviour*>(result.handler);
                    if (source == transitioning->Destination()) {
                        return;
                    }
                    Transition transition{source, transitioning->Destination(), trigger, args};
                    HandleTransitioningTrigger(args, rep, transition);
                    return;
                }
                default:
                    throw std::logic_error("State machine configuration is invalid: unknown trigger handler type.");
            }
        }

        void HandleReentryTrigger(const Args& args, StateRepresentation& representativeState, Transition transition) {
            transition = representativeState.Exit(transition);
            auto& newRepresentation = GetRepresentation(transition.destination);

            if (transition.source != transition.destination) {
                Transition finalSuperExit{transition.destination, transition.destination, transition.trigger, args};
                newRepresentation.Exit(finalSuperExit);

                InvokeTransitioned(finalSuperExit);
                auto* entered = EnterState(newRepresentation, finalSuperExit, args);
                InvokeTransitionCompleted(finalSuperExit);

                SetState(entered->state);
                return;
            }

            InvokeTransitioned(transition);
            auto* entered = EnterState(newRepresentation, transition, args);
            InvokeTransitionCompleted(transition);

            SetState(entered->state);
        }

        void HandleTransitioningTrigger(const Args& args, StateRepresentation& representativeState, Transition transition) {
            transition = representativeState.Exit(transition);

            SetState(transition.destination);
            auto& newRepresentation = GetRepresentation(transition.destination);

            InvokeTransitioned(transition);
            auto* entered = EnterState(newRepresentation, transition, args);

            if (entered->state != State()) {
                SetState(entered->state);
            }

            Transition completed{transition.source, State(), transition.trigger, transition.parameters};
            InvokeTransitionCompleted(completed);
        }

        StateRepresentation* EnterState(StateRepresentation& representation, const Transition& transition, const Args& args) {
            StateRepresentation* current = &representation;
            current->Enter(transition, args);

            if (firing_mode_ == FiringMode::Immediate && State() != transition.destination) {
                current = &GetRepresentation(State());
            }

            if (current->has_initial_transition) {
                bool is_substate = false;
                for (const auto* sub : current->substates) {
                    if (sub->state == current->initial_transition_target) {
                        is_substate = true;
                        break;
                    }
                }

                if (!is_substate) {
                    throw std::logic_error("Initial transition target must be configured as a substate.");
                }

                InitialTransition initial{transition.source, current->initial_transition_target, transition.trigger, args};
                auto& target = GetRepresentation(current->initial_transition_target);

                Transition pseudo{transition.destination, initial.destination, transition.trigger, transition.parameters};
                InvokeTransitioned(pseudo);

                return EnterState(target, initial, args);
            }

            return current;
        }

        void InvokeTransitioned(const Transition& transition) {
            for (const auto& callback : on_transitioned_) {
                callback(transition);
            }
        }

        void InvokeTransitionCompleted(const Transition& transition) {
            for (const auto& callback : on_transition_completed_) {
                callback(transition);
            }
        }

        std::future<void> InvokeTransitionedAsync(const Transition& transition) {
            InvokeTransitioned(transition);
            std::vector<std::future<void>> futures;
            for (auto& cb : on_transitioned_async_) {
                futures.push_back(cb(transition));
            }
            if (futures.empty()) return ReadyFuture();
            return ChainFutures(std::move(futures));
        }

        std::future<void> InvokeTransitionCompletedAsync(const Transition& transition) {
            InvokeTransitionCompleted(transition);
            std::vector<std::future<void>> futures;
            for (auto& cb : on_transition_completed_async_) {
                futures.push_back(cb(transition));
            }
            if (futures.empty()) return ReadyFuture();
            return ChainFutures(std::move(futures));
        }

        // ── Async internal fire ───────────────────────────────────────────────

        std::future<void> InternalFireAsync(const TTrigger& trigger, const Args& args) {
            // Queued async: enqueue then drain on background thread.
            return std::async(std::launch::async, [this, trigger, args]() mutable {
                async_event_queue_.push(QueuedTrigger{trigger, args});

                if (async_firing_.exchange(true)) {
                    return;  // Another thread is draining.
                }

                try {
                    while (!async_event_queue_.empty()) {
                        // Note: async_event_queue_ is not thread-safe for concurrent push+pop;
                        // for single-producer use this pattern is safe.
                        auto queued = async_event_queue_.front();
                        async_event_queue_.pop();
                        InternalFireOneAsync(queued.trigger, queued.args).get();
                    }
                } catch (...) {
                    async_firing_.store(false);
                    throw;
                }
                async_firing_.store(false);
            });
        }

        std::future<void> InternalFireOneAsync(const TTrigger& trigger, const Args& args) {
            auto configured = trigger_configuration_.find(trigger);
            if (configured != trigger_configuration_.end()) {
                configured->second.ValidateParameters(args);
            }

            TState source = State();
            auto& rep = GetRepresentation(source);

            TriggerBehaviourResult result;
            if (!rep.TryFindHandler(trigger, args, result) || !result.unmet_guard_conditions.empty()) {
                unhandled_trigger_action_(rep.state, trigger, result.unmet_guard_conditions);
                return ReadyFuture();
            }

            if (result.handler == nullptr) {
                throw std::logic_error("State machine configuration is invalid: null trigger handler.");
            }

            // Already on a background thread — execute synchronously and await user futures.
            switch (result.handler->Kind()) {
                case BehaviourKind::Ignored:
                    return ReadyFuture();
                case BehaviourKind::Internal: {
                    auto* internal = static_cast<InternalTriggerBehaviour*>(result.handler);
                    Transition transition{source, source, trigger, args};
                    internal->Execute(transition, args);
                    return ReadyFuture();
                }
                case BehaviourKind::Reentry: {
                    auto* reentry = static_cast<ReentryTriggerBehaviour*>(result.handler);
                    Transition transition{source, reentry->Destination(), trigger, args};
                    HandleReentryTriggerAsync(args, rep, transition);
                    return ReadyFuture();
                }
                case BehaviourKind::Dynamic: {
                    auto* dynamic = static_cast<DynamicTriggerBehaviour*>(result.handler);
                    const auto destination = dynamic->GetDestinationState(args);
                    Transition transition{source, destination, trigger, args};
                    HandleTransitioningTriggerAsync(args, rep, transition);
                    return ReadyFuture();
                }
                case BehaviourKind::Transitioning: {
                    auto* transitioning = static_cast<TransitioningTriggerBehaviour*>(result.handler);
                    if (source == transitioning->Destination()) return ReadyFuture();
                    Transition transition{source, transitioning->Destination(), trigger, args};
                    HandleTransitioningTriggerAsync(args, rep, transition);
                    return ReadyFuture();
                }
                default:
                    throw std::logic_error("Unknown trigger handler type.");
            }
        }

        void HandleReentryTriggerAsync(const Args& args, StateRepresentation& rep, Transition transition) {
            transition = rep.Exit(transition);
            rep.ExitAsync(transition).get();
            auto& newRep = GetRepresentation(transition.destination);

            if (transition.source != transition.destination) {
                Transition fe{transition.destination, transition.destination, transition.trigger, args};
                newRep.ExitAsync(fe).get();
                InvokeTransitionedAsync(fe).get();
                auto* entered = EnterStateAsync(newRep, fe, args);
                InvokeTransitionCompletedAsync(fe).get();
                SetState(entered->state);
            } else {
                InvokeTransitionedAsync(transition).get();
                auto* entered = EnterStateAsync(newRep, transition, args);
                InvokeTransitionCompletedAsync(transition).get();
                SetState(entered->state);
            }
        }

        void HandleTransitioningTriggerAsync(const Args& args, StateRepresentation& rep, Transition transition) {
            rep.ExitAsync(transition).get();
            SetState(transition.destination);
            auto& newRep = GetRepresentation(transition.destination);
            InvokeTransitionedAsync(transition).get();
            auto* entered = EnterStateAsync(newRep, transition, args);
            if (entered->state != State()) SetState(entered->state);
            Transition completed{transition.source, State(), transition.trigger, transition.parameters};
            InvokeTransitionCompletedAsync(completed).get();
        }

        StateRepresentation* EnterStateAsync(StateRepresentation& representation, const Transition& transition, const Args& args) {
            StateRepresentation* current = &representation;
            current->EnterAsync(transition, args).get();

            if (current->has_initial_transition) {
                bool is_substate = false;
                for (const auto* sub : current->substates) {
                    if (sub->state == current->initial_transition_target) { is_substate = true; break; }
                }
                if (!is_substate) throw std::logic_error("Initial transition target must be a substate.");

                InitialTransition initial{transition.source, current->initial_transition_target, transition.trigger, args};
                auto& target = GetRepresentation(current->initial_transition_target);
                Transition pseudo{transition.destination, initial.destination, transition.trigger, transition.parameters};
                InvokeTransitionedAsync(pseudo).get();
                return EnterStateAsync(target, initial, args);
            }
            return current;
        }

        [[noreturn]] void DefaultUnhandledTriggerAction(const TState& state,
                                                        const TTrigger& trigger,
                                                        const std::vector<std::string>& unmetGuardConditions) {
            if (!unmetGuardConditions.empty()) {
                std::string message = "No valid transition for trigger in current state; unmet guards: ";
                for (std::size_t i = 0; i < unmetGuardConditions.size(); ++i) {
                    message += unmetGuardConditions[i];
                    if (i + 1 != unmetGuardConditions.size()) {
                        message += ", ";
                    }
                }
                throw std::logic_error(message);
            }

            (void)state;
            (void)trigger;
            throw std::logic_error("No valid transition is configured for this trigger in the current state.");
        }

    private:
        FiringMode firing_mode_ = FiringMode::Queued;
        std::unordered_map<TState, std::unique_ptr<StateRepresentation>> state_configuration_;
        std::unordered_map<TTrigger, TriggerWithParameters> trigger_configuration_;

        std::function<TState()> state_accessor_;
        std::function<void(const TState&)> state_mutator_;

        TState state_storage_{};
        TState initial_state_{};

        std::function<void(const TState&, const TTrigger&, const std::vector<std::string>&)> unhandled_trigger_action_;
        std::vector<std::function<void(const Transition&)>> on_transitioned_;
        std::vector<std::function<void(const Transition&)>> on_transition_completed_;
        std::vector<std::function<std::future<void>(const Transition&)>> on_transitioned_async_;
        std::vector<std::function<std::future<void>(const Transition&)>> on_transition_completed_async_;

        std::queue<QueuedTrigger> event_queue_;
        bool firing_ = false;

        std::queue<QueuedTrigger> async_event_queue_;
        std::atomic<bool> async_firing_{false};
    };
}  // namespace Clownfish


#endif // CLOWNFISH_HPP
