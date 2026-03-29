#include "swarm_coordinator.h"

#include <algorithm>
#include <future>

#include "agent.h"

SwarmCoordinator::SwarmCoordinator(ThreadPool& pool, AgentFactory factory)
    : pool_(pool), agent_factory_(std::move(factory)) {}

SwarmCoordinator::SubTaskResult SwarmCoordinator::run_single_task(
    const SubTask& task) {
    try {
        std::unique_ptr<Agent> worker = agent_factory_();
        if (!worker) {
            return {task.description, "Failed to create worker agent", false};
        }

        std::string prompt = task.description;
        if (!task.workspace_scope.empty()) {
            prompt = "Working in subdirectory: " + task.workspace_scope +
                     "\n\n" + prompt;
        }

        const std::string result = worker->run_turn(prompt);
        return {task.description, result, true};
    } catch (const std::exception& e) {
        return {task.description, std::string("Worker error: ") + e.what(), false};
    }
}

std::vector<SwarmCoordinator::SubTaskResult> SwarmCoordinator::execute_parallel(
    const std::vector<SubTask>& tasks) {
    if (tasks.empty()) return {};

    // Limit concurrency
    const std::size_t batch_size = std::min(tasks.size(), max_workers_);
    std::vector<SubTaskResult> all_results;
    all_results.reserve(tasks.size());

    for (std::size_t start = 0; start < tasks.size(); start += batch_size) {
        const std::size_t end = std::min(start + batch_size, tasks.size());
        std::vector<std::future<SubTaskResult>> futures;

        for (std::size_t i = start; i < end; ++i) {
            const auto& task = tasks[i];
            futures.push_back(pool_.submit(
                [this, &task]() { return run_single_task(task); }));
        }

        for (auto& future : futures) {
            all_results.push_back(future.get());
        }
    }

    return all_results;
}

std::vector<SwarmCoordinator::SubTaskResult> SwarmCoordinator::execute_sequential(
    const std::vector<SubTask>& tasks) {
    std::vector<SubTaskResult> results;
    results.reserve(tasks.size());

    for (const auto& task : tasks) {
        results.push_back(run_single_task(task));
    }

    return results;
}
