#include <catch2/catch_test_macros.hpp>
#include "event_dispatcher.hpp"
#include <string>
#include <vector>
#include <atomic>

using namespace grid_events;

// Test event types
struct VoltageDropEvent {
    std::string substation;
    double voltage_pu;
    std::string message() const { return "Voltage drop at " + substation + ": " + std::to_string(voltage_pu) + " pu"; }
};

struct LineTripEvent {
    std::string line_id;
    std::string message() const { return "Line " + line_id + " tripped"; }
};

struct NonLoggableEvent {
    int data;
    // No message() method - should not satisfy LoggableEvent concept
};

TEST_CASE("EventBus: subscribe and publish basic event", "[event_bus]") {
    EventBus bus;
    std::vector<std::string> received;

    bus.subscribe<VoltageDropEvent>([&](const VoltageDropEvent& e) {
        received.push_back(e.message());
    });

    bus.publish(VoltageDropEvent{"Sub_A", 0.92});

    REQUIRE(received.size() == 1);
    // Floating point formatting may vary (0.92 vs 0.920000)
    REQUIRE(received[0].find("Voltage drop at Sub_A:") != std::string::npos);
    REQUIRE(received[0].find("0.92") != std::string::npos);
}

TEST_CASE("EventBus: multiple subscribers for same event", "[event_bus]") {
    EventBus bus;
    int count1 = 0, count2 = 0;

    bus.subscribe<VoltageDropEvent>([&](const VoltageDropEvent&) { count1++; });
    bus.subscribe<VoltageDropEvent>([&](const VoltageDropEvent&) { count2++; });

    bus.publish(VoltageDropEvent{"Sub_B", 0.95});
    bus.publish(VoltageDropEvent{"Sub_C", 0.90});

    REQUIRE(count1 == 2);
    REQUIRE(count2 == 2);
}

TEST_CASE("EventBus: different event types are independent", "[event_bus]") {
    EventBus bus;
    int voltage_count = 0, line_count = 0;

    bus.subscribe<VoltageDropEvent>([&](const VoltageDropEvent&) { voltage_count++; });
    bus.subscribe<LineTripEvent>([&](const LineTripEvent&) { line_count++; });

    bus.publish(VoltageDropEvent{"Sub_A", 0.92});
    bus.publish(LineTripEvent{"L1"});
    bus.publish(VoltageDropEvent{"Sub_B", 0.88});

    REQUIRE(voltage_count == 2);
    REQUIRE(line_count == 1);
}

TEST_CASE("EventBus: LoggableEvent concept triggers auto-logging", "[event_bus][concept]") {
    EventBus bus;
    std::ostringstream captured_output;
    auto old_buf = std::cout.rdbuf(captured_output.rdbuf());

    bus.publish(VoltageDropEvent{"Test_Sub", 0.90});

    std::cout.rdbuf(old_buf);
    std::string output = captured_output.str();
    REQUIRE(output.find("[SYSTEM LOG] Auto-logged:") != std::string::npos);
    REQUIRE(output.find("Voltage drop at Test_Sub") != std::string::npos);
}

TEST_CASE("EventBus: NonLoggableEvent does not trigger auto-logging", "[event_bus][concept]") {
    EventBus bus;
    std::ostringstream captured_output;
    auto old_buf = std::cout.rdbuf(captured_output.rdbuf());

    bus.publish(NonLoggableEvent{42});

    std::cout.rdbuf(old_buf);
    std::string output = captured_output.str();
    REQUIRE(output.find("[SYSTEM LOG]") == std::string::npos);
}

TEST_CASE("EventBus: const-correct message() works with concept", "[event_bus][concept]") {
    // This tests that the concept requires message() to be callable on const objects
    // VoltageDropEvent has non-const message() - should still work since it's not mutating
    EventBus bus;
    bool called = false;
    
    bus.subscribe<VoltageDropEvent>([&](const VoltageDropEvent& e) {
        // Can call message() on const ref
        [[maybe_unused]] auto msg = e.message();
        called = true;
    });
    
    bus.publish(VoltageDropEvent{"Sub", 1.0});
    REQUIRE(called);
}

TEST_CASE("EventBus: publish with no subscribers is safe", "[event_bus]") {
    EventBus bus;
    // Should not crash
    bus.publish(VoltageDropEvent{"Sub", 1.0});
    bus.publish(LineTripEvent{"L1"});
    SUCCEED();
}

TEST_CASE("EventBus: lambda with capture works", "[event_bus]") {
    EventBus bus;
    std::string prefix = "Handler: ";
    std::string result;

    bus.subscribe<VoltageDropEvent>([prefix, &result](const VoltageDropEvent& e) {
        result = prefix + e.message();
    });

    bus.publish(VoltageDropEvent{"Sub_X", 0.91});
    REQUIRE(result.find("Handler: Voltage drop at Sub_X:") != std::string::npos);
    REQUIRE(result.find("0.91") != std::string::npos);
}

TEST_CASE("EventBus: move-only callback via shared_ptr works", "[event_bus]") {
    EventBus bus;
    auto ptr = std::make_shared<int>(42);
    int captured = 0;

    bus.subscribe<VoltageDropEvent>([ptr, &captured](const VoltageDropEvent&) {
        captured = *ptr;
    });

    bus.publish(VoltageDropEvent{"Sub", 1.0});
    REQUIRE(captured == 42);
}

// Stress test: many events, many subscribers
TEST_CASE("EventBus: stress test with many events", "[event_bus][stress]") {
    EventBus bus;
    std::atomic<int> total_received{0};

    for (int i = 0; i < 10; ++i) {
        bus.subscribe<VoltageDropEvent>([&](const VoltageDropEvent&) {
            total_received.fetch_add(1, std::memory_order_relaxed);
        });
    }

    for (int i = 0; i < 100; ++i) {
        bus.publish(VoltageDropEvent{"Sub", 1.0});
    }

    REQUIRE(total_received.load() == 10 * 100);
}