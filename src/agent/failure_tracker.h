#ifndef AGENT_FAILURE_TRACKER_H
#define AGENT_FAILURE_TRACKER_H

#include <sstream>
#include <string>
#include <vector>

/// Tracks consecutive tool failures to detect stuck loops and trigger recovery.
class FailureTracker {
public:
    explicit FailureTracker(int max_consecutive = 3)
        : max_consecutive_(max_consecutive) {}

    void record_failure(const std::string& tool_name, const std::string& error) {
        ++consecutive_failures_;
        ++total_failures_;
        recent_failures_.push_back({tool_name, error});
        if (recent_failures_.size() > 10) {
            recent_failures_.erase(recent_failures_.begin());
        }
    }

    void record_success() {
        consecutive_failures_ = 0;
    }

    bool is_stuck() const {
        if (consecutive_failures_ < max_consecutive_) return false;

        // Check if the same tool is failing repeatedly
        if (recent_failures_.size() >= 2) {
            const auto& last = recent_failures_.back();
            const auto& prev = recent_failures_[recent_failures_.size() - 2];
            if (last.tool_name == prev.tool_name) return true;
        }

        return consecutive_failures_ >= max_consecutive_;
    }

    int consecutive_failures() const { return consecutive_failures_; }
    int total_failures() const { return total_failures_; }

    std::string diagnostic() const {
        std::ostringstream report;
        report << "AGENT STUCK: " << consecutive_failures_
               << " consecutive failures detected.\n";
        report << "Recent failures:\n";
        const std::size_t start = recent_failures_.size() > 3
            ? recent_failures_.size() - 3 : 0;
        for (std::size_t i = start; i < recent_failures_.size(); ++i) {
            report << "  - " << recent_failures_[i].tool_name << ": "
                   << recent_failures_[i].error.substr(0, 200) << "\n";
        }
        report << "\nRecovery suggestions:\n";
        report << "  - Try a completely different approach\n";
        report << "  - Break the task into smaller steps\n";
        report << "  - Read the relevant code again before editing\n";
        report << "  - If stuck, explain the problem to the user and ask for guidance\n";
        return report.str();
    }

    void reset() {
        consecutive_failures_ = 0;
        total_failures_ = 0;
        recent_failures_.clear();
    }

private:
    struct FailureEntry {
        std::string tool_name;
        std::string error;
    };

    int consecutive_failures_ = 0;
    int total_failures_ = 0;
    int max_consecutive_;
    std::vector<FailureEntry> recent_failures_;
};

#endif
