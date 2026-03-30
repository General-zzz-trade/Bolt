#ifndef AGENT_SWARM_COORDINATOR_H
#define AGENT_SWARM_COORDINATOR_H

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/interfaces/command_runner.h"
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

    /// A task assigned to a worker agent in team mode with its own git worktree.
    struct WorkerTask {
        std::string description;
        std::string branch_name;    // auto-generated
        std::string worktree_path;  // auto-generated
    };

    /// Result from a worker agent in team mode.
    struct WorkerResult {
        std::string task;
        std::string branch_name;
        bool success = false;
        std::string summary;
        std::string diff;           // git diff output
        int files_changed = 0;
    };

    using AgentFactory = std::function<std::unique_ptr<Agent>()>;

    SwarmCoordinator(ThreadPool& pool, AgentFactory factory);

    /// Execute multiple sub-tasks in parallel using separate agent instances.
    /// Each agent runs a single turn with the given sub-task description.
    std::vector<SubTaskResult> execute_parallel(const std::vector<SubTask>& tasks);

    /// Execute sub-tasks sequentially (useful when tasks depend on each other).
    std::vector<SubTaskResult> execute_sequential(const std::vector<SubTask>& tasks);

    /// Execute tasks in Agent Team mode: each task gets its own git worktree
    /// and branch, tasks run in parallel, and results include diffs.
    std::vector<WorkerResult> execute_team(
        const std::vector<std::string>& tasks,
        const std::filesystem::path& workspace_root,
        std::shared_ptr<ICommandRunner> command_runner);

    /// Maximum number of concurrent workers.
    void set_max_workers(std::size_t max) { max_workers_ = max; }

private:
    ThreadPool& pool_;
    AgentFactory agent_factory_;
    std::size_t max_workers_ = 4;

    SubTaskResult run_single_task(const SubTask& task);

    // Worktree helpers for team mode
    std::string create_worktree(const std::filesystem::path& workspace_root,
                                const std::string& branch_name,
                                std::shared_ptr<ICommandRunner> command_runner) const;
    bool remove_worktree(const std::string& worktree_path,
                         std::shared_ptr<ICommandRunner> command_runner) const;
    std::string generate_branch_name(const std::string& task) const;
};

#endif
