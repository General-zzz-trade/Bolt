#include "task_planner_tool.h"

#include <sstream>
#include <stdexcept>

#include "swarm_coordinator.h"

namespace {

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    return s.substr(begin, s.find_last_not_of(" \t\r\n") - begin + 1);
}

}  // namespace

TaskPlannerTool::~TaskPlannerTool() = default;

std::string TaskPlannerTool::name() const { return "task_planner"; }

void TaskPlannerTool::set_swarm_support(ThreadPool* pool, AgentFactory factory) {
    if (pool && factory) {
        swarm_ = std::make_unique<SwarmCoordinator>(*pool, std::move(factory));
    }
}

std::string TaskPlannerTool::description() const {
    return "Plan and track multi-step tasks. Commands: "
           "'plan:<steps>' to create a plan, "
           "'done:<step_number>' to mark complete, "
           "'status' to view progress, "
           "'parallel:<task1>\\n<task2>' to run sub-tasks concurrently.";
}

ToolSchema TaskPlannerTool::schema() const {
    return {name(), description(), {
        {"command", "string", "plan:<task> | done:<step_number> | status", true},
    }};
}

ToolResult TaskPlannerTool::run(const std::string& args) const {
    const std::string input = trim(args);

    if (input.rfind("plan:", 0) == 0 || input.rfind("plan=", 0) == 0 ||
        input.rfind("command=plan:", 0) == 0) {
        std::string task = input;
        // Strip prefix
        for (const char* prefix : {"command=plan:", "plan:", "plan="}) {
            if (task.rfind(prefix, 0) == 0) {
                task = trim(task.substr(std::string(prefix).size()));
                break;
            }
        }
        return handle_plan(task);
    }

    if (input.rfind("done:", 0) == 0 || input.rfind("done=", 0) == 0 ||
        input.rfind("command=done:", 0) == 0) {
        std::string step_str = input;
        for (const char* prefix : {"command=done:", "done:", "done="}) {
            if (step_str.rfind(prefix, 0) == 0) {
                step_str = trim(step_str.substr(std::string(prefix).size()));
                break;
            }
        }
        return handle_done(step_str);
    }

    if (input == "status" || input == "command=status") {
        return handle_status();
    }

    if (input.rfind("parallel:", 0) == 0 || input.rfind("parallel=", 0) == 0 ||
        input.rfind("command=parallel:", 0) == 0) {
        std::string tasks_text = input;
        for (const char* prefix : {"command=parallel:", "parallel:", "parallel="}) {
            if (tasks_text.rfind(prefix, 0) == 0) {
                tasks_text = trim(tasks_text.substr(std::string(prefix).size()));
                break;
            }
        }
        return handle_parallel(tasks_text);
    }

    return {false, "Unknown command. Use: plan:<task>, done:<step>, status, or parallel:<tasks>"};
}

ToolResult TaskPlannerTool::handle_plan(const std::string& task) const {
    task_description_ = task;
    steps_.clear();

    // The model will see this output and use it to guide its next steps.
    // We create placeholder steps — the model's own reasoning fills them in
    // through the conversation. But we provide a structured starting point.
    //
    // For a real implementation, this would call the model to decompose.
    // Here we provide the framework for the model to fill in via conversation.

    std::ostringstream result;
    result << "TASK: " << task << "\n\n";
    result << "Plan created. Now break this task into concrete steps by calling:\n";
    result << "  task_planner with args: plan:<step1 description>\\n<step2 description>\\n...\n\n";
    result << "Or describe your plan in your reply and I'll track each action as you execute it.\n";
    result << "Use 'done:<step_number>' after completing each step.\n";

    // If the task has newlines, treat each line as a step
    std::istringstream lines(task);
    std::string line;
    int num = 0;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty()) continue;
        // Strip leading "1." or "- " numbering
        if (line.size() > 2 && line[1] == '.' && line[0] >= '0' && line[0] <= '9') {
            line = trim(line.substr(2));
        } else if (line.size() > 2 && line[1] == ')') {
            line = trim(line.substr(2));
        } else if (line.rfind("- ", 0) == 0) {
            line = trim(line.substr(2));
        }
        if (!line.empty()) {
            steps_.push_back({++num, line, false});
        }
    }

    if (!steps_.empty()) {
        result.str("");
        result << "TASK: " << task_description_ << "\n\n";
        result << format_plan();
        result << "\nStart with step 1. Call 'done:1' when complete.\n";
    }

    return {true, result.str()};
}

ToolResult TaskPlannerTool::handle_done(const std::string& step_str) const {
    try {
        const int step_num = std::stoi(step_str);
        for (auto& step : steps_) {
            if (step.number == step_num) {
                step.completed = true;

                // Find next incomplete step
                int next = -1;
                for (const auto& s : steps_) {
                    if (!s.completed) {
                        next = s.number;
                        break;
                    }
                }

                std::ostringstream result;
                result << "Step " << step_num << " marked complete.\n\n";
                result << format_plan();

                if (next > 0) {
                    result << "\nNext: step " << next << "\n";
                } else {
                    result << "\nAll steps complete! Task finished.\n";
                }
                return {true, result.str()};
            }
        }
        return {false, "Step " + step_str + " not found in plan."};
    } catch (...) {
        return {false, "Invalid step number: " + step_str};
    }
}

ToolResult TaskPlannerTool::handle_status() const {
    if (steps_.empty()) {
        return {true, "No active plan. Use 'plan:<task description>' to create one."};
    }

    std::ostringstream result;
    result << "TASK: " << task_description_ << "\n\n";
    result << format_plan();

    int done = 0;
    for (const auto& s : steps_) {
        if (s.completed) ++done;
    }
    result << "\nProgress: " << done << "/" << steps_.size() << " steps complete\n";

    return {true, result.str()};
}

ToolResult TaskPlannerTool::handle_parallel(const std::string& tasks_text) const {
    if (!swarm_) {
        return {false, "Parallel execution is not available. "
                       "No agent factory configured for worker agents."};
    }

    // Parse newline-separated task descriptions
    std::vector<SwarmCoordinator::SubTask> sub_tasks;
    std::istringstream lines(tasks_text);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty()) continue;
        // Strip leading numbering
        if (line.size() > 2 && line[1] == '.' && line[0] >= '0' && line[0] <= '9') {
            line = trim(line.substr(2));
        } else if (line.rfind("- ", 0) == 0) {
            line = trim(line.substr(2));
        }
        if (!line.empty()) {
            sub_tasks.push_back({line, ""});
        }
    }

    if (sub_tasks.empty()) {
        return {false, "No tasks provided. Format: parallel:<task1>\\n<task2>\\n..."};
    }

    std::ostringstream result;
    result << "PARALLEL EXECUTION: " << sub_tasks.size() << " sub-tasks\n\n";

    const auto results = swarm_->execute_parallel(sub_tasks);

    int succeeded = 0;
    for (std::size_t i = 0; i < results.size(); ++i) {
        result << "--- Task " << (i + 1) << ": " << results[i].description << " ---\n";
        result << "Status: " << (results[i].success ? "OK" : "FAILED") << "\n";
        // Truncate long results
        const std::string& content = results[i].result;
        if (content.size() > 500) {
            result << content.substr(0, 500) << "\n... (truncated)\n";
        } else {
            result << content << "\n";
        }
        result << "\n";
        if (results[i].success) ++succeeded;
    }

    result << "SUMMARY: " << succeeded << "/" << results.size() << " tasks succeeded\n";
    return {true, result.str()};
}

std::string TaskPlannerTool::format_plan() const {
    std::ostringstream out;
    out << "PLAN:\n";
    for (const auto& step : steps_) {
        out << "  " << (step.completed ? "[x]" : "[ ]")
            << " " << step.number << ". " << step.description << "\n";
    }
    return out.str();
}
