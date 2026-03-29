#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../src/app/web_approval_provider.h"

namespace {

void expect_true(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_equal(bool actual, bool expected, const std::string& message) {
    if (actual != expected) {
        throw std::runtime_error(message + " (expected " + (expected ? "true" : "false") +
                                 ", got " + (actual ? "true" : "false") + ")");
    }
}

// --- Tests ---

void expect_initial_snapshot_has_no_pending_request() {
    WebApprovalProvider provider;
    auto snap = provider.snapshot();
    expect_equal(snap.has_pending_request, false,
                 "initial snapshot should have no pending request");
}

void expect_approve_blocks_until_resolve_called() {
    WebApprovalProvider provider;
    std::atomic<bool> approve_returned{false};

    ApprovalRequest request;
    request.tool_name = "test_tool";

    std::thread worker([&] {
        provider.approve(request);
        approve_returned.store(true);
    });

    // Give the worker thread time to block
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    expect_equal(approve_returned.load(), false,
                 "approve should block until resolve is called");

    // Verify snapshot shows pending request
    auto snap = provider.snapshot();
    expect_equal(snap.has_pending_request, true,
                 "snapshot should show pending request while blocked");

    provider.resolve(true);
    worker.join();

    expect_equal(approve_returned.load(), true,
                 "approve should have returned after resolve");
}

void expect_resolve_true_makes_approve_return_true() {
    WebApprovalProvider provider;
    bool result = false;

    ApprovalRequest request;
    request.tool_name = "write_file";

    std::thread worker([&] {
        result = provider.approve(request);
    });

    // Wait for the worker to block
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    provider.resolve(true);
    worker.join();

    expect_equal(result, true, "approve should return true when resolved with true");
}

void expect_resolve_false_makes_approve_return_false() {
    WebApprovalProvider provider;
    bool result = true;

    ApprovalRequest request;
    request.tool_name = "run_command";

    std::thread worker([&] {
        result = provider.approve(request);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    provider.resolve(false);
    worker.join();

    expect_equal(result, false, "approve should return false when resolved with false");
}

void expect_resolve_without_pending_returns_false() {
    WebApprovalProvider provider;
    bool result = provider.resolve(true);
    expect_equal(result, false,
                 "resolve without pending request should return false");
}

void expect_snapshot_reflects_pending_request_state() {
    WebApprovalProvider provider;

    // Initially no pending request
    auto snap1 = provider.snapshot();
    expect_equal(snap1.has_pending_request, false,
                 "no pending request initially");

    ApprovalRequest request;
    request.tool_name = "edit_file";
    request.args = "--path /tmp/test.txt";

    std::thread worker([&] {
        provider.approve(request);
    });

    // Wait for approve to set up the pending request
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto snap2 = provider.snapshot();
    expect_equal(snap2.has_pending_request, true,
                 "should have pending request during approve");
    expect_true(snap2.request.tool_name == "edit_file",
                "snapshot should reflect the pending tool name");

    provider.resolve(true);
    worker.join();

    // After resolve, no pending request
    auto snap3 = provider.snapshot();
    expect_equal(snap3.has_pending_request, false,
                 "no pending request after resolve");
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"initial snapshot has no pending request",
         expect_initial_snapshot_has_no_pending_request},
        {"approve blocks until resolve called",
         expect_approve_blocks_until_resolve_called},
        {"resolve(true) makes approve return true",
         expect_resolve_true_makes_approve_return_true},
        {"resolve(false) makes approve return false",
         expect_resolve_false_makes_approve_return_false},
        {"resolve without pending request returns false",
         expect_resolve_without_pending_returns_false},
        {"snapshot reflects pending request state",
         expect_snapshot_reflects_pending_request_state},
    };

    std::size_t passed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            ++passed;
            std::cout << "[PASS] " << test.first << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << test.first << ": " << error.what() << '\n';
            return 1;
        }
    }

    std::cout << "Passed " << passed << " approval provider tests.\n";
    return 0;
}
