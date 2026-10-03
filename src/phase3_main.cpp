#include "event_dispatcher.hpp"
#include <iostream>
#include <format>
#include <string>

// --- Event Type 1: A normal struct ---
struct VoltageDropEvent {
    std::string substation;
    double voltage_pu;
};

// --- Event Type 2: Satisfies the LoggableEvent concept ---
struct LineTripEvent {
    std::string line_id;
    
    // Because this method exists and returns a string, 
    // the compiler recognizes LineTripEvent as a "LoggableEvent".
    std::string message() const {
        return std::format("CRITICAL: {} has tripped. N-1 constraint violated.", line_id);
    }
};

int main() {
    std::cout << "=== Phase 3: Generic Event Dispatcher (C++20 Concepts) ===\n\n";
    grid_events::EventBus bus;

    // Subscribe to Voltage drops
    bus.subscribe<VoltageDropEvent>([](const VoltageDropEvent& e) {
        std::cout << std::format("  -> [Controller A] Detected {} voltage drop to {:.2f} pu\n", 
                                 e.substation, e.voltage_pu);
    });

    // Subscribe to Line trips
    bus.subscribe<LineTripEvent>([](const LineTripEvent& e) {
        std::cout << std::format("  -> [Controller B] Dispatching repair crew to {}\n", e.line_id);
    });

    // --- Publishing Events ---
    
    std::cout << "--- Publishing VoltageDropEvent ---\n";
    // This is NOT a LoggableEvent, so 'if constexpr' ignores the auto-log.
    bus.publish(VoltageDropEvent{"Substation_Alpha", 0.92});

    std::cout << "\n--- Publishing LineTripEvent ---\n";
    // This IS a LoggableEvent. The bus will auto-log it before triggering the handlers!
    bus.publish(LineTripEvent{"Line_L42"});

    // ========================================================================
    // THE POWER OF CONCEPTS (Error Prevention)
    // ========================================================================
    /* 
       If you were to uncomment the line below, the compiler would throw a clean error.
       Why? Because the lambda takes an `int`, but we told it to subscribe to `VoltageDropEvent`.
       The `requires std::invocable<Func, const EventType&>` concept catches this instantly!
    */
    // bus.subscribe<VoltageDropEvent>([](int wrong_type) { });

    return 0;
}
