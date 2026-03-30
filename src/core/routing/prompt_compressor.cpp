#include "prompt_compressor.h"

#include <algorithm>
#include <sstream>

PromptCompressor::PromptCompressor() : config_() {}

PromptCompressor::PromptCompressor(Config config)
    : config_(std::move(config)) {}

std::vector<ChatMessage> PromptCompressor::compress(
    const std::vector<ChatMessage>& messages) const {

    // Step 1: Trim to max history window (preserves all recent context)
    std::vector<ChatMessage> result = trim_history(messages);

    // Step 2: Summarize old tool results (keep recent ones in full)
    if (config_.summarize_old_tool_results) {
        result = summarize_old_results(std::move(result));
    }

    // Step 3: Lossless truncation only if configured (default: off)
    if (config_.max_tool_result_chars > 0) {
        for (auto& msg : result) {
            if (msg.role == ChatRole::tool) {
                msg.content = truncate_tool_result(msg.content);
            }
        }
    }

    // Step 4: Deduplicate whitespace (lossless — removes only redundant blank lines)
    if (config_.deduplicate_whitespace) {
        for (auto& msg : result) {
            std::string& content = msg.content;
            // Replace 3+ consecutive newlines with 2
            std::string cleaned;
            cleaned.reserve(content.size());
            int consecutive_newlines = 0;
            for (char ch : content) {
                if (ch == '\n') {
                    ++consecutive_newlines;
                    if (consecutive_newlines <= 2) {
                        cleaned += ch;
                    }
                } else {
                    consecutive_newlines = 0;
                    cleaned += ch;
                }
            }
            content = std::move(cleaned);
        }
    }

    // Step 5: Hard cap only if configured (default: off for lossless)
    if (config_.max_total_chars > 0) {
        std::size_t total_chars = 0;
        for (const auto& msg : result) {
            total_chars += msg.content.size();
        }

        while (total_chars > config_.max_total_chars && result.size() > 2) {
            auto it = result.begin();
            while (it != result.end() && it->role == ChatRole::system) ++it;
            if (it == result.end()) break;
            total_chars -= it->content.size();
            result.erase(it);
        }
    }

    return result;
}

std::size_t PromptCompressor::estimate_tokens(
    const std::vector<ChatMessage>& messages) {
    std::size_t chars = 0;
    for (const auto& msg : messages) {
        chars += msg.content.size();
        for (const auto& tc : msg.tool_calls) {
            chars += tc.arguments.size() + tc.name.size();
        }
    }
    return chars / 4;  // ~4 chars per token for English
}

std::string PromptCompressor::truncate_tool_result(const std::string& content) const {
    if (content.size() <= config_.max_tool_result_chars) {
        return content;
    }

    // Keep first portion + "... (truncated N chars)"
    const std::size_t keep = config_.max_tool_result_chars - 40;
    return content.substr(0, keep) + "\n... (truncated " +
           std::to_string(content.size() - keep) + " chars)";
}

std::string PromptCompressor::summarize_tool_result(const std::string& content,
                                                    const std::string& tool_name) {
    // Extract the status line
    const bool is_error = content.rfind("TOOL_ERROR", 0) == 0;
    const bool is_policy = content.rfind("POLICY_DENY", 0) == 0;
    const bool is_approval = content.rfind("APPROVAL_DENY", 0) == 0;

    if (is_error) {
        // Keep the error status + first few lines of the error message
        std::string summary = "TOOL_ERROR [" + tool_name + "]: ";
        const auto first_nl = content.find('\n');
        if (first_nl == std::string::npos) return content;

        // Extract error-relevant lines (lines containing "error", "fail", "not found")
        std::istringstream lines(content.substr(first_nl + 1));
        std::string line;
        int kept = 0;
        while (std::getline(lines, line) && kept < 5) {
            // Keep lines that look like error messages
            const std::string lower = [&]() {
                std::string s = line;
                for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return s;
            }();
            if (lower.find("error") != std::string::npos ||
                lower.find("fail") != std::string::npos ||
                lower.find("not found") != std::string::npos ||
                lower.find("exception") != std::string::npos ||
                kept == 0) {  // Always keep first line
                summary += line + "\n";
                ++kept;
            }
        }
        return summary;
    }

    if (is_policy || is_approval) {
        // Keep denial messages in full (they're short)
        return content;
    }

    // TOOL_OK — summarize to status + first line of content
    std::string summary = "TOOL_OK [" + tool_name + "]";
    const auto first_nl = content.find('\n');
    if (first_nl != std::string::npos && first_nl + 1 < content.size()) {
        const auto second_nl = content.find('\n', first_nl + 1);
        const std::string first_line = content.substr(first_nl + 1,
            second_nl == std::string::npos ? std::string::npos : second_nl - first_nl - 1);
        if (!first_line.empty()) {
            summary += ": " + first_line;
        }
        const auto remaining = content.size() - first_nl;
        if (remaining > 200) {
            summary += " (..." + std::to_string(remaining) + " chars omitted)";
        }
    }
    return summary;
}

std::vector<ChatMessage> PromptCompressor::summarize_old_results(
    std::vector<ChatMessage> messages) const {
    // Count tool messages from the end to find which ones are "recent"
    std::size_t tool_count = 0;
    std::vector<bool> is_recent(messages.size(), false);

    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role == ChatRole::tool) {
            ++tool_count;
            if (tool_count <= config_.recent_tool_results_to_keep) {
                is_recent[std::distance(messages.begin(),
                    std::next(it).base())] = true;
            }
        }
    }

    // Summarize old tool results
    for (std::size_t i = 0; i < messages.size(); ++i) {
        if (messages[i].role == ChatRole::tool && !is_recent[i]) {
            messages[i].content = summarize_tool_result(
                messages[i].content, messages[i].name);
        }
    }

    return messages;
}

std::vector<ChatMessage> PromptCompressor::trim_history(
    const std::vector<ChatMessage>& messages) const {

    if (messages.size() <= config_.max_history_messages) {
        return messages;
    }

    // Always keep system message(s) + last N messages
    std::vector<ChatMessage> result;

    // Collect system messages
    for (const auto& msg : messages) {
        if (msg.role == ChatRole::system) {
            result.push_back(msg);
        }
    }

    // Keep last N non-system messages, but respect tool chain boundaries
    const std::size_t keep = config_.max_history_messages - result.size();
    if (messages.size() > keep) {
        std::size_t start = messages.size() - keep;

        // Adjust start forward if it lands on a tool message (orphaned from its assistant)
        // or backward to include the assistant that owns tool results at the start
        while (start > 0 && start < messages.size() &&
               messages[start].role == ChatRole::tool) {
            --start;  // Include the assistant message that has the tool_calls
        }

        for (std::size_t i = start; i < messages.size(); ++i) {
            if (messages[i].role != ChatRole::system) {
                result.push_back(messages[i]);
            }
        }
    }

    return result;
}
