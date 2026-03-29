#ifndef AGENT_SWARM_COORDINATOR_H
#define AGENT_SWARM_COORDINATOR_H

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/threading/thread_pool.h"

class Agent;

/// Coordinates parallel execution of sub-tasks across multiple agent instances.
/// Each worker agent gets its own conversation history but shares the workspace.
class SwarmCoordinator {
public:
    struct SubTask {
        std::string description;
        std::string workspace_scope;  // Optional subdirectory scope
    };

    struct SubTaskResult {
        std::string description;
        std::string result;
        bool success;
    };

    using AgentFactory = std::function<std::unique_ptr<Agent>()>;

    SwarmCoordinator(ThreadPool& pool, AgentFactory factory);

    /// Execute multiple sub-tasks in parallel using separate agent instances.
    /// Each agent runs a single turn with the given sub-task description.
    std::vector<SubTaskResult> execute_parallel(const std::vector<SubTask>& tasks);

    /// Execute sub-tasks sequentially (useful when tasks depend on each other).
    std::vector<SubTaskResult> execute_sequential(const std::vector<SubTask>& tasks);

    /// Maximum number of concurrent workers.
    void set_max_workers(std::size_t max) { max_workers_ = max; }

private:
    ThreadPool& pool_;
    AgentFactory agent_factory_;
    std::size_t max_workers_ = 4;

    SubTaskResult run_single_task(const SubTask& task);
};

#endif
