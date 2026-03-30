#ifndef AGENT_SPECULATIVE_EXECUTOR_H
#define AGENT_SPECULATIVE_EXECUTOR_H

#include <atomic>
#include <future>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "tool.h"
#include "tool_registry.h"
#include "../core/threading/thread_pool.h"

/// Speculative (predictive) tool executor.
/// During LLM streaming, detects tool call patterns early and starts executing
/// read-only tools before the full response arrives.  When the agent later
/// dispatches the same tool call, the result is already available.
class SpeculativeExecutor {
public:
    SpeculativeExecutor(const ToolRegistry& tools, ThreadPool& pool);

    /// Called on each streaming token accumulation.
    /// Analyzes partial response for tool calls and speculatively executes read-only tools.
    void on_token(const std::string& accumulated);

    /// Check if a tool result is already available from speculation.
    /// Returns empty string if not available.
    std::string get_result(const std::string& tool_name, const std::string& args) const;

    /// Check if speculation is in progress for this tool.
    bool is_pending(const std::string& tool_name, const std::string& args) const;

    /// Wait for a speculative result (with timeout).
    /// Returns the tool output content, or empty string on timeout / miss.
    std::string wait_result(const std::string& tool_name, const std::string& args,
                            int timeout_ms = 5000) const;

    /// Clear all speculative results (call at start of each turn).
    void reset();

    /// Stats
    int hits() const { return hits_.load(); }
    int misses() const { return misses_.load(); }

private:
    const ToolRegistry& tools_;
    ThreadPool& pool_;

    struct SpecResult {
        std::string key;              // tool_name + '\0' + args
        std::shared_future<ToolResult> future;
        bool completed = false;
        ToolResult result;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<SpecResult>> results_;
    std::string last_analyzed_;       // avoid re-analyzing the same prefix

    mutable std::atomic<int> hits_{0};
    mutable std::atomic<int> misses_{0};

    static std::string make_key(const std::string& name, const std::string& args);

    /// Parse partial streaming output for tool call patterns.
    struct DetectedCall {
        std::string tool_name;
        std::string args;
    };
    std::vector<DetectedCall> detect_tool_calls(const std::string& text) const;

    void submit_speculation(const std::string& tool_name, const std::string& args);
};

#endif
