#include "delete_file_tool.h"

#include <stdexcept>

#include "workspace_utils.h"

namespace {

void safe_log_audit(const std::shared_ptr<IAuditLogger>& audit_logger, const AuditEvent& event) {
    if (audit_logger == nullptr) {
        return;
    }

    try {
        audit_logger->log(event);
    } catch (...) {
    }
}

}  // namespace

DeleteFileTool::DeleteFileTool(std::filesystem::path workspace_root,
                               std::shared_ptr<IFileSystem> file_system,
                               std::shared_ptr<IAuditLogger> audit_logger)
    : workspace_root_(std::filesystem::weakly_canonical(std::move(workspace_root))),
      file_system_(std::move(file_system)),
      audit_logger_(std::move(audit_logger)) {
    if (file_system_ == nullptr) {
        throw std::invalid_argument("DeleteFileTool requires a file system");
    }
}

std::string DeleteFileTool::name() const {
    return "delete_file";
}

std::string DeleteFileTool::description() const {
    return "Delete a file from the workspace. Args: relative file path";
}

ToolPreview DeleteFileTool::preview(const std::string& args) const {
    try {
        const std::string path = trim_copy(args);
        const std::filesystem::path target = resolve_workspace_path(workspace_root_, path);
        const std::string relative_path = workspace_relative_path(workspace_root_, target);

        ToolPreview preview;
        preview.summary = "Delete file " + relative_path;
        preview.details = "path: " + relative_path;
        return preview;
    } catch (const std::exception& e) {
        return {"Unable to preview delete_file request", e.what()};
    }
}

ToolResult DeleteFileTool::run(const std::string& args) const {
    try {
        const std::string path = trim_copy(args);
        if (path.empty()) {
            return {false, "delete_file path is empty"};
        }

        const std::filesystem::path target = resolve_workspace_path(workspace_root_, path);
        const std::string relative_path = workspace_relative_path(workspace_root_, target);

        if (!is_within_workspace(target, workspace_root_)) {
            safe_log_audit(audit_logger_,
                           {"file", "validation_failed", name(), path,
                            workspace_root_.string(), 0, -1, false, false, false,
                            "Path is outside the workspace"});
            return {false, "Path is outside the workspace"};
        }

        if (!file_system_->exists(target)) {
            safe_log_audit(audit_logger_,
                           {"file", "validation_failed", name(), relative_path,
                            workspace_root_.string(), 0, -1, false, false, false,
                            "File does not exist"});
            return {false, "File does not exist: " + relative_path};
        }

        if (file_system_->is_directory(target)) {
            safe_log_audit(audit_logger_,
                           {"file", "validation_failed", name(), relative_path,
                            workspace_root_.string(), 0, -1, false, false, false,
                            "Target path is a directory"});
            return {false, "Target path is a directory"};
        }

        const FileDeleteResult delete_result = file_system_->remove_file(target);
        if (!delete_result.success) {
            safe_log_audit(audit_logger_,
                           {"file", "tool_error", name(), relative_path,
                            workspace_root_.string(), 0, -1, false, false, false,
                            delete_result.error});
            return {false, delete_result.error};
        }

        safe_log_audit(audit_logger_,
                       {"file", "executed", name(), relative_path, workspace_root_.string(),
                        0, -1, true, true, false, ""});

        return {true, "DELETED FILE: " + relative_path};
    } catch (const std::exception& e) {
        safe_log_audit(audit_logger_,
                       {"file", "tool_error", name(), "", workspace_root_.string(), 0, -1,
                        false, false, false, e.what()});
        return {false, e.what()};
    }
}
