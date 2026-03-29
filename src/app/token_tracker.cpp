#include "token_tracker.h"

#include <iomanip>
#include <sstream>

void TokenTracker::record_turn(const TokenUsage& usage, const std::string& model) {
    TurnRecord record;
    record.usage = usage;
    record.model = model;
    record.timestamp = std::chrono::steady_clock::now();
    turns_.push_back(std::move(record));

    cumulative_.input_tokens += usage.input_tokens;
    cumulative_.output_tokens += usage.output_tokens;
    cumulative_.cache_creation_tokens += usage.cache_creation_tokens;
    cumulative_.cache_read_tokens += usage.cache_read_tokens;
}

TokenUsage TokenTracker::last_turn() const {
    if (turns_.empty()) {
        return {};
    }
    return turns_.back().usage;
}

TokenUsage TokenTracker::total() const {
    return cumulative_;
}

int TokenTracker::turn_count() const {
    return static_cast<int>(turns_.size());
}

double TokenTracker::input_price_per_mtok(const std::string& model) {
    // Local/free models
    if (model.find("ollama") != std::string::npos ||
        model.find("llama") != std::string::npos ||
        model.find("qwen") != std::string::npos ||
        model.find("mistral") != std::string::npos ||
        model.find("phi") != std::string::npos ||
        model.find("codellama") != std::string::npos) {
        return 0.0;
    }

    // Claude models
    if (model.find("claude-sonnet") != std::string::npos ||
        model.find("claude-3-5-sonnet") != std::string::npos) {
        return 3.0;
    }
    if (model.find("claude-opus") != std::string::npos ||
        model.find("claude-3-opus") != std::string::npos) {
        return 15.0;
    }
    if (model.find("claude-haiku") != std::string::npos ||
        model.find("claude-3-haiku") != std::string::npos) {
        return 0.25;
    }

    // OpenAI models
    if (model.find("gpt-4o") != std::string::npos) {
        return 2.50;
    }
    if (model.find("gpt-4-turbo") != std::string::npos) {
        return 10.0;
    }
    if (model.find("gpt-3.5") != std::string::npos) {
        return 0.50;
    }

    // Gemini models
    if (model.find("gemini-2.0-flash") != std::string::npos ||
        model.find("gemini-1.5-flash") != std::string::npos) {
        return 0.075;
    }
    if (model.find("gemini-1.5-pro") != std::string::npos ||
        model.find("gemini-2.0-pro") != std::string::npos) {
        return 1.25;
    }

    // Default fallback
    return 1.0;
}

double TokenTracker::output_price_per_mtok(const std::string& model) {
    // Local/free models
    if (model.find("ollama") != std::string::npos ||
        model.find("llama") != std::string::npos ||
        model.find("qwen") != std::string::npos ||
        model.find("mistral") != std::string::npos ||
        model.find("phi") != std::string::npos ||
        model.find("codellama") != std::string::npos) {
        return 0.0;
    }

    // Claude models
    if (model.find("claude-sonnet") != std::string::npos ||
        model.find("claude-3-5-sonnet") != std::string::npos) {
        return 15.0;
    }
    if (model.find("claude-opus") != std::string::npos ||
        model.find("claude-3-opus") != std::string::npos) {
        return 75.0;
    }
    if (model.find("claude-haiku") != std::string::npos ||
        model.find("claude-3-haiku") != std::string::npos) {
        return 1.25;
    }

    // OpenAI models
    if (model.find("gpt-4o") != std::string::npos) {
        return 10.0;
    }
    if (model.find("gpt-4-turbo") != std::string::npos) {
        return 30.0;
    }
    if (model.find("gpt-3.5") != std::string::npos) {
        return 1.50;
    }

    // Gemini models
    if (model.find("gemini-2.0-flash") != std::string::npos ||
        model.find("gemini-1.5-flash") != std::string::npos) {
        return 0.30;
    }
    if (model.find("gemini-1.5-pro") != std::string::npos ||
        model.find("gemini-2.0-pro") != std::string::npos) {
        return 5.0;
    }

    // Default fallback
    return 3.0;
}

double TokenTracker::estimated_cost() const {
    double cost = 0.0;
    for (const auto& turn : turns_) {
        double in_price = input_price_per_mtok(turn.model);
        double out_price = output_price_per_mtok(turn.model);

        cost += (turn.usage.input_tokens / 1000000.0) * in_price;
        cost += (turn.usage.output_tokens / 1000000.0) * out_price;
        // Cache creation is billed at input rate (1.25x for Claude, but we simplify)
        cost += (turn.usage.cache_creation_tokens / 1000000.0) * in_price;
        // Cache reads are typically cheaper, use 0.1x input rate
        cost += (turn.usage.cache_read_tokens / 1000000.0) * in_price * 0.1;
    }
    return cost;
}

static std::string format_number_with_commas(int value) {
    std::string num = std::to_string(value);
    int insert_pos = static_cast<int>(num.length()) - 3;
    while (insert_pos > 0) {
        num.insert(insert_pos, ",");
        insert_pos -= 3;
    }
    return num;
}

std::string TokenTracker::format_cost() const {
    std::ostringstream oss;
    oss << "$" << std::fixed << std::setprecision(4) << estimated_cost();
    return oss.str();
}

std::string TokenTracker::format_tokens() const {
    std::ostringstream oss;
    oss << "In: " << format_number_with_commas(cumulative_.input_tokens)
        << "  Out: " << format_number_with_commas(cumulative_.output_tokens);
    return oss.str();
}

std::string TokenTracker::format_summary() const {
    std::ostringstream oss;
    oss << "Turns: " << turn_count() << "\n";
    oss << "Input tokens:  " << format_number_with_commas(cumulative_.input_tokens) << "\n";
    oss << "Output tokens: " << format_number_with_commas(cumulative_.output_tokens) << "\n";
    if (cumulative_.cache_creation_tokens > 0) {
        oss << "Cache write:   " << format_number_with_commas(cumulative_.cache_creation_tokens) << "\n";
    }
    if (cumulative_.cache_read_tokens > 0) {
        oss << "Cache read:    " << format_number_with_commas(cumulative_.cache_read_tokens) << "\n";
    }
    oss << "Estimated cost: " << format_cost();
    return oss.str();
}
