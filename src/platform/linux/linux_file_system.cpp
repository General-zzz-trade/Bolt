#include "linux_file_system.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace {

bool should_skip_directory_name(const std::string& name) {
    static const std::unordered_set<std::string> skipped = {
        ".git", ".idea", "build", "cmake-build-debug", "node_modules", "__pycache__",
    };
    return skipped.count(name) > 0;
}

bool is_text_like_file(const std::filesystem::path& path) {
    static const std::unordered_set<std::string> exts = {
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
        ".txt", ".md", ".cmake", ".json", ".toml", ".yaml", ".yml", ".py",
        ".js", ".ts", ".tsx", ".java", ".cs", ".rs", ".go", ".html",
        ".css", ".xml", ".ini", ".sh", ".bash", ".zsh", ".fish",
    };
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return exts.count(ext) > 0;
}

}  // namespace

bool LinuxFileSystem::exists(const std::filesystem::path& p) const {
    return std::filesystem::exists(p);
}

bool LinuxFileSystem::is_directory(const std::filesystem::path& p) const {
    return std::filesystem::is_directory(p);
}

bool LinuxFileSystem::is_regular_file(const std::filesystem::path& p) const {
    return std::filesystem::is_regular_file(p);
}

DirectoryListResult LinuxFileSystem::list_directory(const std::filesystem::path& path) const {
    try {
        std::vector<DirectoryEntryInfo> entries;
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            DirectoryEntryInfo info;
            info.is_directory = entry.is_directory();
            info.name = entry.path().filename().string();
            info.size = entry.is_regular_file() ? entry.file_size() : 0;
            entries.push_back(info);
        }
        return {true, std::move(entries), ""};
    } catch (const std::exception& e) {
        return {false, {}, e.what()};
    }
}

bool LinuxFileSystem::create_directories(const std::filesystem::path& path,
                                          std::string& error) const {
    try {
        if (path.empty()) return true;
        std::filesystem::create_directories(path);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

FileReadResult LinuxFileSystem::read_text_file(const std::filesystem::path& path) const {
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) return {false, "", "Unable to open file"};
        std::ostringstream buf;
        buf << input.rdbuf();
        return {true, buf.str(), ""};
    } catch (const std::exception& e) {
        return {false, "", e.what()};
    }
}

FileWriteResult LinuxFileSystem::write_text_file(const std::filesystem::path& path,
                                                  const std::string& content) const {
    try {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) return {false, "Unable to open file for writing"};
        output << content;
        output.close();
        return {true, ""};
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
}

TextSearchResult LinuxFileSystem::search_text(const std::filesystem::path& root,
                                               const std::string& query,
                                               std::size_t max_matches,
                                               std::uintmax_t max_file_bytes) const {
    try {
        std::vector<TextSearchMatch> matches;
        bool truncated = false;

        std::filesystem::recursive_directory_iterator it(
            root, std::filesystem::directory_options::skip_permission_denied);
        for (const auto& entry : it) {
            if (entry.is_directory() &&
                should_skip_directory_name(entry.path().filename().string())) {
                it.disable_recursion_pending();
                continue;
            }
            if (!entry.is_regular_file() || !is_text_like_file(entry.path())) continue;

            std::error_code ec;
            if (std::filesystem::file_size(entry.path(), ec) > max_file_bytes) continue;

            std::ifstream input(entry.path());
            if (!input) continue;

            std::string line;
            std::size_t line_num = 0;
            while (std::getline(input, line)) {
                ++line_num;
                if (line.find(query) == std::string::npos) continue;
                matches.push_back({
                    std::filesystem::relative(entry.path(), root).string(),
                    line_num, line});
                if (matches.size() >= max_matches) {
                    return {true, std::move(matches), true, ""};
                }
            }
        }
        return {true, std::move(matches), truncated, ""};
    } catch (const std::exception& e) {
        return {false, {}, false, e.what()};
    }
}
