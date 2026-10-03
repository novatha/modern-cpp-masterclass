#pragma once
#include <iostream>
#include <vector>
#include <functional>
#include <concepts>
#include <typeindex>
#include <unordered_map>
#include <memory>
#include <string>

namespace grid_events {

// ============================================================================
// 1. DEFINING A CUSTOM C++20 CONCEPT
// ============================================================================
// A Concept is a compile-time contract. 
// Here, we define that any type 'T' can be considered a "LoggableEvent" 
// ONLY IF it has a method called .message() that returns a std::string.
template <typename T>
concept LoggableEvent = requires(T a) {
    { a.message() } -> std::convertible_to<std::string>;
};

// ============================================================================
// 2. A GENERIC EVENT DISPATCHER
// ============================================================================
class EventBus {
private:
    // We use a classic C++ trick (Type Erasure) to store a list of handlers 
    // for ANY data type in a single map, without knowing the types in advance.
    struct HandlerBase { virtual ~HandlerBase() = default; };
    
    template <typename EventType>
    struct Handler : public HandlerBase {
        std::vector<std::function<void(const EventType&)>> callbacks;
    };

    std::unordered_map<std::type_index, std::unique_ptr<HandlerBase>> subscribers_;

public:
    // ============================================================================
    // 3. USING STANDARD CONCEPTS (std::invocable)
    // ============================================================================
    // Instead of accepting ANY type for 'Func', we strictly constrain it.
    // 'Func' MUST be a callable function/lambda that accepts 'const EventType&'.
    // If you pass the wrong function signature, the compiler gives a clean error.
    template <typename EventType, typename Func>
    requires std::invocable<Func, const EventType&>
    void subscribe(Func&& callback) {
        auto idx = std::type_index(typeid(EventType));
        
        if (!subscribers_.contains(idx)) {
            subscribers_[idx] = std::make_unique<Handler<EventType>>();
        }
        
        auto* handler = static_cast<Handler<EventType>*>(subscribers_[idx].get());
        handler->callbacks.push_back(std::forward<Func>(callback));
    }

    // ============================================================================
    // 4. COMPILE-TIME BRANCHING (if constexpr)
    // ============================================================================
    template <typename EventType>
    void publish(const EventType& event) {
        
        // This is evaluated DURING COMPILATION.
        // If the EventType satisfies our 'LoggableEvent' concept, this code is compiled.
        // If not, the compiler completely erases these 3 lines from the final binary!
        if constexpr (LoggableEvent<EventType>) {
            std::cout << "[SYSTEM LOG] Auto-logged: " << event.message() << "\n";
        }

        // Trigger all subscribed functions
        auto idx = std::type_index(typeid(EventType));
        if (subscribers_.contains(idx)) {
            auto* handler = static_cast<Handler<EventType>*>(subscribers_[idx].get());
            for (const auto& cb : handler->callbacks) {
                cb(event);
            }
        }
    }
};

} // namespace grid_events
