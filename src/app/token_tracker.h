#pragma once
#include <chrono>
#include <string>
#include <vector>

#include "../core/model/chat_message.h"

class TokenTracker {
public:
    void record_turn(const TokenUsage& usage, const std::string& model);

    TokenUsage last_turn() const;
    TokenUsage total() const;
    int turn_count() const;

    double estimated_cost() const;
    std::string format_cost() const;
    std::string format_tokens() const;
    std::string format_summary() const;

    static double input_price_per_mtok(const std::string& model);
    static double output_price_per_mtok(const std::string& model);

private:
    struct TurnRecord {
        TokenUsage usage;
        std::string model;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::vector<TurnRecord> turns_;
    TokenUsage cumulative_;
};
