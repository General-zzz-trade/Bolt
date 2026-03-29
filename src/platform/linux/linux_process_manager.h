#ifndef PLATFORM_LINUX_LINUX_PROCESS_MANAGER_H
#define PLATFORM_LINUX_LINUX_PROCESS_MANAGER_H

#include "../../core/interfaces/process_manager.h"

class LinuxProcessManager : public IProcessManager {
public:
    ProcessListResult list_processes() const override;
    LaunchProcessResult launch_process(const std::string& command_line) const override;
};

#endif
