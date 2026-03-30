#include "speculative_executor.h"

#include <chrono>
#include <nlohmann/json.hpp>

SpeculativeExecutor::SpeculativeExecutor(const ToolRegistry& tools, ThreadPool& pool)
    : tools_(tools), pool_(pool) {}

std::string SpeculativeExecutor::make_key(const std::string& name, const std::string& args) {
    return name + std::string(1, '\0') + args;
}

void SpeculativeExecutor::on_token(const std::string& accumulated) {
    // Only re-analyze every 100 characters of new content
    if (accumulated.size() < 100 ||
        accumulated.size() - last_analyzed_.size() < 100) {
        return;
    }
    last_analyzed_ = accumulated;

    auto calls = detect_tool_calls(accumulated);
    for (const auto& call : calls) {
        submit_speculation(call.tool_name, call.args);
    }
}

std::vector<SpeculativeExecutor::DetectedCall>
SpeculativeExecutor::detect_tool_calls(const std::string& text) const {
    std::vector<DetectedCall> calls;

    // Pattern: Structured tool calls from function-calling APIs.
    // Look for "name": "<tool_name>" followed by "arguments": "..." or "arguments": {...}
    std::string::size_type pos = 0;
    while (pos < text.size()) {
        auto name_key = text.find("\"name\"", pos);
        if (name_key == std::string::npos) break;

        // Find the colon after "name"
        auto colon = text.find(':', name_key + 6);
        if (colon == std::string::npos) break;

        // Find opening quote of tool name value
        auto quote1 = text.find('"', colon + 1);
        if (quote1 == std::string::npos) break;
        auto quote2 = text.find('"', quote1 + 1);
        if (quote2 == std::string::npos) break;

        std::string tool_name = text.substr(quote1 + 1, quote2 - quote1 - 1);

        // Check if this is a known read-only tool
        const Tool* tool = tools_.find(tool_name);
        if (!tool || !tool->is_read_only()) {
            pos = quote2 + 1;
            continue;
        }

        // Find "arguments" key nearby (within 200 chars)
        auto args_key = text.find("\"arguments\"", quote2);
        if (args_key == std::string::npos || args_key - quote2 > 200) {
            pos = quote2 + 1;
            continue;
        }

        auto args_colon = text.find(':', args_key + 11);
        if (args_colon == std::string::npos) {
            pos = args_key + 11;
            continue;
        }

        // Arguments can be a JSON string (escaped) or direct object
        auto args_start = text.find_first_not_of(" \t\r\n", args_colon + 1);
        if (args_start == std::string::npos) {
            pos = args_colon + 1;
            continue;
        }

        std::string args;
        std::string::size_type args_end = args_start;

        if (text[args_start] == '"') {
            // JSON string value -- find closing quote (handle escapes)
            auto end = args_start + 1;
            while (end < text.size()) {
                if (text[end] == '\\') { end += 2; continue; }
                if (text[end] == '"') break;
                ++end;
            }
            if (end >= text.size()) {
                pos = args_start + 1;
                continue;
            }

            // Unescape the JSON string
            std::string raw = text.substr(args_start + 1, end - args_start - 1);
            std::string unescaped;
            unescaped.reserve(raw.size());
            for (std::size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '\\' && i + 1 < raw.size()) {
                    switch (raw[i + 1]) {
                        case '"':  unescaped += '"';  ++i; break;
                        case '\\': unescaped += '\\'; ++i; break;
                        case 'n':  unescaped += '\n'; ++i; break;
                        case 't':  unescaped += '\t'; ++i; break;
                        case '/':  unescaped += '/';  ++i; break;
                        default:   unescaped += raw[i]; break;
                    }
                } else {
                    unescaped += raw[i];
                }
            }
            args = unescaped;
            args_end = end + 1;
        } else if (text[args_start] == '{') {
            // Direct JSON object -- find matching closing brace
            int depth = 0;
            auto end = args_start;
            for (; end < text.size(); ++end) {
                if (text[end] == '{') ++depth;
                if (text[end] == '}') {
                    --depth;
                    if (depth == 0) { ++end; break; }
                }
            }
            if (depth != 0) {
                pos = args_start + 1;
                continue;
            }
            args = text.substr(args_start, end - args_start);
            args_end = end;
        } else {
            pos = args_start + 1;
            continue;
        }

        // Try to simplify: if the JSON object has a single string value,
        // extract it (e.g. {"path": "src/main.cpp"} -> "src/main.cpp").
        // This matches how agent.cpp parses tool call arguments for simple tools.
        try {
            auto j = nlohmann::json::parse(args);
            if (j.is_object() && j.size() == 1 && j.begin()->is_string()) {
                args = j.begin()->get<std::string>();
            }
        } catch (...) {
            // Use args as-is if not valid JSON
        }

        if (!args.empty()) {
            calls.push_back({tool_name, args});
        }

        pos = args_end;
    }

    return calls;
}

void SpeculativeExecutor::submit_speculation(const std::string& tool_name,
                                              const std::string& args) {
    const std::string key = make_key(tool_name, args);

    std::lock_guard<std::mutex> lock(mutex_);
    if (results_.count(key)) return;  // Already submitted

    const Tool* tool = tools_.find(tool_name);
    if (!tool || !tool->is_read_only()) return;

    auto spec = std::make_shared<SpecResult>();
    spec->key = key;

    // Submit to thread pool and store shared_future so multiple readers can wait
    auto tool_ptr = tool;
    auto args_copy = args;
    std::future<ToolResult> fut = pool_.submit([tool_ptr, args_copy]() -> ToolResult {
        return tool_ptr->run(args_copy);
    });
    spec->future = fut.share();

    results_[key] = spec;
}

std::string SpeculativeExecutor::get_result(const std::string& tool_name,
                                             const std::string& args) const {
    const std::string key = make_key(tool_name, args);

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = results_.find(key);
    if (it == results_.end()) return "";

    auto& spec = it->second;
    if (spec->completed) {
        return spec->result.content;
    }

    // Check if the future is ready without blocking
    if (spec->future.valid() &&
        spec->future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        spec->result = spec->future.get();
        spec->completed = true;
        return spec->result.content;
    }

    return "";
}

bool SpeculativeExecutor::is_pending(const std::string& tool_name,
                                      const std::string& args) const {
    const std::string key = make_key(tool_name, args);

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = results_.find(key);
    if (it == results_.end()) return false;
    return !it->second->completed;
}

std::string SpeculativeExecutor::wait_result(const std::string& tool_name,
                                              const std::string& args,
                                              int timeout_ms) const {
    const std::string key = make_key(tool_name, args);
    std::shared_ptr<SpecResult> spec;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = results_.find(key);
        if (it == results_.end()) {
            misses_.fetch_add(1, std::memory_order_relaxed);
            return "";
        }
        spec = it->second;
    }

    if (spec->completed) {
        hits_.fetch_add(1, std::memory_order_relaxed);
        return spec->result.content;
    }

    if (!spec->future.valid()) {
        misses_.fetch_add(1, std::memory_order_relaxed);
        return "";
    }

    auto status = spec->future.wait_for(std::chrono::milliseconds(timeout_ms));
    if (status != std::future_status::ready) {
        misses_.fetch_add(1, std::memory_order_relaxed);
        return "";
    }

    spec->result = spec->future.get();
    spec->completed = true;
    hits_.fetch_add(1, std::memory_order_relaxed);
    return spec->result.content;
}

void SpeculativeExecutor::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    results_.clear();
    last_analyzed_.clear();
}
