#include "code_intel_tool.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

#include "workspace_utils.h"

namespace {

bool is_source_file(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    static const std::vector<std::string> exts = {
        ".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".hxx",
        ".py", ".js", ".ts", ".tsx", ".jsx",
        ".rs", ".go", ".java", ".kt", ".cs",
        ".rb", ".swift", ".m", ".mm"
    };
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

std::string make_relative(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::error_code ec;
    auto rel = std::filesystem::relative(path, root, ec);
    return ec ? path.string() : rel.string();
}

}  // namespace

CodeIntelTool::CodeIntelTool(std::filesystem::path workspace_root,
                             std::shared_ptr<IFileSystem> file_system)
    : workspace_root_(std::filesystem::weakly_canonical(std::move(workspace_root))),
      file_system_(std::move(file_system)) {
    if (!file_system_) {
        throw std::invalid_argument("CodeIntelTool requires a file system");
    }
}

std::string CodeIntelTool::name() const { return "code_intel"; }

std::string CodeIntelTool::description() const {
    return "Code intelligence: find definitions, classes, references, and function signatures. "
           "Commands: find_def:<symbol>, find_class:<name>, find_refs:<symbol>, "
           "find_includes:<file>, list_functions:<path>";
}

ToolSchema CodeIntelTool::schema() const {
    return {name(), description(), {
        {"command", "string",
         "find_def:<symbol> | find_class:<name> | find_refs:<symbol> | "
         "find_includes:<file> | list_functions:<filepath>", true},
    }};
}

ToolResult CodeIntelTool::run(const std::string& args) const {
    std::string input = trim_copy(args);

    // Strip "command=" prefix if present
    if (input.rfind("command=", 0) == 0) {
        input = trim_copy(input.substr(8));
    }

    // Check result cache first
    auto now = std::chrono::steady_clock::now();
    auto cache_it = result_cache_.find(input);
    if (cache_it != result_cache_.end()) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - cache_it->second.timestamp).count();
        if (age < RESULT_CACHE_TTL_SECONDS) {
            return cache_it->second.result;
        }
        result_cache_.erase(cache_it);
    }

    ToolResult result = {false, "Unknown command. Use: find_def:<symbol>, find_class:<name>, "
                                "find_refs:<symbol>, find_includes:<file>, list_functions:<path>"};

    if (input.rfind("find_def:", 0) == 0) {
        result = find_definition(trim_copy(input.substr(9)));
    } else if (input.rfind("find_class:", 0) == 0) {
        result = find_class(trim_copy(input.substr(11)));
    } else if (input.rfind("find_refs:", 0) == 0) {
        result = find_references(trim_copy(input.substr(10)));
    } else if (input.rfind("find_includes:", 0) == 0) {
        result = find_includes(trim_copy(input.substr(14)));
    } else if (input.rfind("list_functions:", 0) == 0) {
        result = list_functions(trim_copy(input.substr(15)));
    } else {
        return result;  // Don't cache errors
    }

    // Store in result cache (evict if too large)
    if (result_cache_.size() >= RESULT_CACHE_MAX_SIZE) {
        // Evict oldest entry
        auto oldest = result_cache_.begin();
        for (auto it = result_cache_.begin(); it != result_cache_.end(); ++it) {
            if (it->second.timestamp < oldest->second.timestamp) {
                oldest = it;
            }
        }
        result_cache_.erase(oldest);
    }
    result_cache_[input] = {result, now};

    return result;
}

ToolResult CodeIntelTool::find_definition(const std::string& symbol) const {
    const std::string pattern =
        std::string("(?:^|\\s)(?:") +
        "(?:[\\w:*&<>]+\\s+)?" + symbol + "\\s*\\([^)]*\\)\\s*(?:const\\s*)?(?:override\\s*)?[{]|" +
        "def\\s+" + symbol + "\\s*\\(|" +
        "function\\s+" + symbol + "\\s*\\(|" +
        "(?:const|let|var)\\s+" + symbol + "\\s*=|" +
        "fn\\s+" + symbol + "\\s*[<(]|" +
        "func\\s+" + symbol + "\\s*\\(" +
        ")";

    auto matches = search_pattern(pattern, 20);

    if (matches.empty()) {
        return {true, "No definition found for '" + symbol + "'"};
    }

    std::ostringstream result;
    result << "Definitions of '" << symbol << "' (" << matches.size() << " found):\n\n";
    for (const auto& m : matches) {
        result << "  " << m.file << ":" << m.line << "\n";
        result << "    " << m.content << "\n";
    }
    return {true, result.str()};
}

ToolResult CodeIntelTool::find_class(const std::string& class_name) const {
    const std::string pattern =
        std::string("(?:") +
        "(?:class|struct|enum)\\s+" + class_name + "\\s*(?:[:{]|$)|" +
        "class\\s+" + class_name + "\\s*[:(]|" +
        "(?:interface|type)\\s+" + class_name + "\\s*[{<]|" +
        "struct\\s+" + class_name + "\\s*[{<]" +
        ")";

    auto matches = search_pattern(pattern, 10);

    if (matches.empty()) {
        return {true, "No class/struct found for '" + class_name + "'"};
    }

    std::ostringstream result;
    result << "Class/struct '" << class_name << "' (" << matches.size() << " found):\n\n";
    for (const auto& m : matches) {
        result << "  " << m.file << ":" << m.line << "\n";
        result << "    " << m.content << "\n";
    }
    return {true, result.str()};
}

ToolResult CodeIntelTool::find_references(const std::string& symbol) const {
    // Simple: find all lines containing the symbol as a whole word
    const std::string pattern = "\\b" + symbol + "\\b";
    auto matches = search_pattern(pattern, 30);

    if (matches.empty()) {
        return {true, "No references found for '" + symbol + "'"};
    }

    std::ostringstream result;
    result << "References to '" << symbol << "' (" << matches.size() << " found):\n\n";
    for (const auto& m : matches) {
        result << "  " << m.file << ":" << m.line << "  " << m.content << "\n";
    }
    return {true, result.str()};
}

ToolResult CodeIntelTool::find_includes(const std::string& filename) const {
    // Simple: find lines that include/import this filename
    const std::string pattern =
        std::string("(?:") +
        "#include\\s*[\"<].*" + filename + "|" +
        "(?:import|from)\\s+.*" + filename + "|" +
        "use\\s+.*" + filename + "|" +
        "import\\s+.*" + filename +
        ")";

    auto matches = search_pattern(pattern, 30);

    if (matches.empty()) {
        return {true, "No files include/import '" + filename + "'"};
    }

    std::ostringstream result;
    result << "Files that include '" << filename << "' (" << matches.size() << " found):\n\n";
    for (const auto& m : matches) {
        result << "  " << m.file << ":" << m.line << "  " << m.content << "\n";
    }
    return {true, result.str()};
}

ToolResult CodeIntelTool::list_functions(const std::string& file_path) const {
    const std::filesystem::path target = resolve_workspace_path(workspace_root_, file_path);
    if (!is_within_workspace(target, workspace_root_)) {
        return {false, "Path is outside the workspace"};
    }
    if (!file_system_->exists(target) || !file_system_->is_regular_file(target)) {
        return {false, "File not found: " + file_path};
    }

    // Match function signatures
    const std::string pattern =
        "(?:^|\\n)\\s*(?:"
        "(?:(?:static|virtual|inline|explicit|constexpr|const|unsigned|signed|void|int|bool|"
        "char|float|double|auto|std::\\w+|string|size_t|vector|unique_ptr|shared_ptr)\\s+)*"
        "[\\w:*&<>]+\\s+[\\w:]+\\s*\\([^)]*\\)|"  // C/C++ function
        "def\\s+\\w+\\s*\\([^)]*\\)|"  // Python
        "(?:function|async\\s+function)\\s+\\w+\\s*\\([^)]*\\)|"  // JS
        "fn\\s+\\w+\\s*[<(][^)]*[)>]|"  // Rust
        "func\\s+[\\w.]+\\s*\\([^)]*\\)"  // Go
        ")";

    auto matches = search_in_file(target, pattern);

    if (matches.empty()) {
        return {true, "No functions found in " + file_path};
    }

    std::ostringstream result;
    result << "Functions in " << file_path << " (" << matches.size() << "):\n\n";
    for (const auto& m : matches) {
        result << "  line " << m.line << ": " << m.content << "\n";
    }
    return {true, result.str()};
}

void CodeIntelTool::collect_source_files(const std::filesystem::path& dir,
                                          std::vector<std::filesystem::path>& out) const {
    std::error_code ec;
    auto options = std::filesystem::directory_options::skip_permission_denied;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, options, ec);
         it != std::filesystem::recursive_directory_iterator(); ) {
        const auto& entry = *it;

        // Skip excluded directories entirely (don't descend)
        if (entry.is_directory()) {
            const auto dirname = entry.path().filename().string();
            if (dirname == ".git" || dirname == "build" || dirname == "third_party" ||
                dirname == "node_modules" || dirname == ".cache" || dirname == "__pycache__" ||
                dirname == "target" || dirname == "vendor" || dirname == "dist" ||
                dirname == ".next" || dirname == "CMakeFiles") {
                it.disable_recursion_pending();
                std::error_code skip_ec;
                it.increment(skip_ec);
                continue;
            }
        }

        if (entry.is_regular_file(ec) && is_source_file(entry.path())) {
            out.push_back(entry.path());
        }

        std::error_code inc_ec;
        it.increment(inc_ec);
    }
}

const std::vector<std::filesystem::path>& CodeIntelTool::get_source_files() const {
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::seconds>(
        now - files_cache_time_).count();
    if (cached_files_.empty() || age >= FILES_CACHE_TTL_SECONDS) {
        cached_files_.clear();
        collect_source_files(workspace_root_, cached_files_);
        files_cache_time_ = now;
    }
    return cached_files_;
}

std::vector<CodeIntelTool::Match> CodeIntelTool::search_pattern(
    const std::string& pattern, int max_results) const {

    const auto& files = get_source_files();

    std::vector<Match> results;
    std::regex re(pattern, std::regex::optimize | std::regex::ECMAScript);

    // Extract a plain-text substring from the pattern for fast pre-filtering.
    // Find the longest literal run (no regex metacharacters) to use as a
    // quick rejection test before invoking the expensive std::regex engine.
    std::string literal_hint;
    {
        std::string current;
        for (char c : pattern) {
            if (std::string("\\[](){}.*+?|^$").find(c) != std::string::npos) {
                if (current.size() > literal_hint.size()) {
                    literal_hint = current;
                }
                current.clear();
            } else {
                current += c;
            }
        }
        if (current.size() > literal_hint.size()) {
            literal_hint = current;
        }
    }
    const bool use_hint = literal_hint.size() >= 3;

    for (const auto& file : files) {
        if (static_cast<int>(results.size()) >= max_results) break;

        std::ifstream input(file);
        if (!input) continue;

        std::string line;
        int line_num = 0;
        while (std::getline(input, line) && static_cast<int>(results.size()) < max_results) {
            ++line_num;
            // Fast pre-filter: skip lines that don't contain the literal hint
            if (use_hint && line.find(literal_hint) == std::string::npos) {
                continue;
            }
            try {
                if (std::regex_search(line, re)) {
                    std::string trimmed = trim_copy(line);
                    if (trimmed.size() > 120) trimmed = trimmed.substr(0, 120) + "...";
                    results.push_back({make_relative(workspace_root_, file), line_num, trimmed});
                }
            } catch (...) {
                // Skip regex errors on malformed lines
            }
        }
    }

    return results;
}

std::vector<CodeIntelTool::Match> CodeIntelTool::search_in_file(
    const std::filesystem::path& file, const std::string& pattern) const {

    std::vector<Match> results;
    std::regex re(pattern, std::regex::optimize | std::regex::ECMAScript);

    std::ifstream input(file);
    if (!input) return results;

    std::string line;
    int line_num = 0;
    while (std::getline(input, line)) {
        ++line_num;
        try {
            if (std::regex_search(line, re)) {
                std::string trimmed = trim_copy(line);
                if (trimmed.size() > 120) trimmed = trimmed.substr(0, 120) + "...";
                results.push_back({make_relative(workspace_root_, file), line_num, trimmed});
            }
        } catch (...) {}
    }

    return results;
}
