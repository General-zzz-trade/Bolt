#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../src/core/net/sse_parser.h"

namespace {

void expect_true(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_equal(const std::string& actual, const std::string& expected,
                  const std::string& message) {
    if (actual != expected) {
        throw std::runtime_error(message + " (expected '" + expected + "', got '" + actual + "')");
    }
}

void expect_equal(std::size_t actual, std::size_t expected, const std::string& message) {
    if (actual != expected) {
        throw std::runtime_error(message + " (expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual) + ")");
    }
}

// --- Tests ---

void expect_single_data_event_parsed() {
    std::vector<SseParser::Event> events;
    SseParser parser([&](const SseParser::Event& e) {
        events.push_back(e);
        return true;
    });

    parser.feed("data: hello world\n\n");

    expect_equal(events.size(), std::size_t(1), "should have one event");
    expect_equal(events[0].data, "hello world", "event data mismatch");
    expect_equal(events[0].type, "", "event type should be empty");
}

void expect_multi_line_data_events_concatenated() {
    std::vector<SseParser::Event> events;
    SseParser parser([&](const SseParser::Event& e) {
        events.push_back(e);
        return true;
    });

    parser.feed("data: line one\ndata: line two\ndata: line three\n\n");

    expect_equal(events.size(), std::size_t(1), "should have one event");
    expect_equal(events[0].data, "line one\nline two\nline three",
                 "multi-line data should be joined with newlines");
}

void expect_event_type_field_parsed() {
    std::vector<SseParser::Event> events;
    SseParser parser([&](const SseParser::Event& e) {
        events.push_back(e);
        return true;
    });

    parser.feed("event: message\ndata: payload\n\n");

    expect_equal(events.size(), std::size_t(1), "should have one event");
    expect_equal(events[0].type, "message", "event type mismatch");
    expect_equal(events[0].data, "payload", "event data mismatch");
}

void expect_comment_lines_ignored() {
    std::vector<SseParser::Event> events;
    SseParser parser([&](const SseParser::Event& e) {
        events.push_back(e);
        return true;
    });

    parser.feed(": this is a comment\ndata: actual data\n: another comment\n\n");

    expect_equal(events.size(), std::size_t(1), "should have one event");
    expect_equal(events[0].data, "actual data", "comment should not affect data");
}

void expect_finish_flushes_pending_event() {
    std::vector<SseParser::Event> events;
    SseParser parser([&](const SseParser::Event& e) {
        events.push_back(e);
        return true;
    });

    parser.feed("data: pending\n");
    expect_equal(events.size(), std::size_t(0), "no event yet without blank line");

    parser.finish();
    expect_equal(events.size(), std::size_t(1), "finish should flush pending event");
    expect_equal(events[0].data, "pending", "flushed event data mismatch");
}

void expect_callback_returning_false_stops_parsing() {
    int count = 0;
    SseParser parser([&](const SseParser::Event&) {
        ++count;
        return false;  // stop after first event
    });

    bool result = parser.feed("data: first\n\ndata: second\n\n");

    expect_true(!result, "feed should return false when callback returns false");
    expect_equal(std::size_t(count), std::size_t(1),
                 "only one event should have been dispatched");
}

void expect_crlf_line_endings_handled() {
    std::vector<SseParser::Event> events;
    SseParser parser([&](const SseParser::Event& e) {
        events.push_back(e);
        return true;
    });

    parser.feed("data: crlf test\r\n\r\n");

    expect_equal(events.size(), std::size_t(1), "should handle CR+LF");
    expect_equal(events[0].data, "crlf test", "data should not contain CR");
}

void expect_empty_chunks_no_issues() {
    std::vector<SseParser::Event> events;
    SseParser parser([&](const SseParser::Event& e) {
        events.push_back(e);
        return true;
    });

    bool r1 = parser.feed("");
    bool r2 = parser.feed("");
    bool r3 = parser.feed("data: ok\n\n");

    expect_true(r1, "empty feed should return true");
    expect_true(r2, "empty feed should return true");
    expect_true(r3, "non-empty feed should return true");
    expect_equal(events.size(), std::size_t(1), "should have one event after empty chunks");
    expect_equal(events[0].data, "ok", "data mismatch after empty chunks");
}

void expect_multiple_events_in_one_chunk() {
    std::vector<SseParser::Event> events;
    SseParser parser([&](const SseParser::Event& e) {
        events.push_back(e);
        return true;
    });

    parser.feed("data: first\n\ndata: second\n\ndata: third\n\n");

    expect_equal(events.size(), std::size_t(3), "should have three events");
    expect_equal(events[0].data, "first", "first event data");
    expect_equal(events[1].data, "second", "second event data");
    expect_equal(events[2].data, "third", "third event data");
}

void expect_partial_lines_buffered() {
    std::vector<SseParser::Event> events;
    SseParser parser([&](const SseParser::Event& e) {
        events.push_back(e);
        return true;
    });

    // Feed partial line
    parser.feed("data: hel");
    expect_equal(events.size(), std::size_t(0), "no event from partial line");

    // Complete the line and add blank line
    parser.feed("lo world\n\n");
    expect_equal(events.size(), std::size_t(1), "event after completing line");
    expect_equal(events[0].data, "hello world", "buffered data should be complete");
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"single data event parsed", expect_single_data_event_parsed},
        {"multi-line data events concatenated", expect_multi_line_data_events_concatenated},
        {"event type field parsed", expect_event_type_field_parsed},
        {"comment lines ignored", expect_comment_lines_ignored},
        {"finish flushes pending event", expect_finish_flushes_pending_event},
        {"callback returning false stops parsing", expect_callback_returning_false_stops_parsing},
        {"CR+LF line endings handled", expect_crlf_line_endings_handled},
        {"empty chunks don't cause issues", expect_empty_chunks_no_issues},
        {"multiple events in one chunk", expect_multiple_events_in_one_chunk},
        {"partial lines buffered correctly", expect_partial_lines_buffered},
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

    std::cout << "Passed " << passed << " SSE parser tests.\n";
    return 0;
}
