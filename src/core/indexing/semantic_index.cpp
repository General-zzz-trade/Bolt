#include "semantic_index.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

void SemanticIndex::build(const std::filesystem::path& root_dir,
                           const std::vector<std::string>& extensions) {
    file_summaries_.clear();
    all_symbols_.clear();
    ready_ = false;

    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(
             root_dir, std::filesystem::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file()) continue;

        const auto ext = entry.path().extension().string();
        bool matched = false;
        for (const auto& allowed : extensions) {
            if (ext == allowed) { matched = true; break; }
        }
        if (!matched) continue;

        // Skip build directories and hidden directories
        const std::string path_str = entry.path().string();
        if (path_str.find("build") != std::string::npos ||
            path_str.find(".git") != std::string::npos ||
            path_str.find("node_modules") != std::string::npos ||
            path_str.find("third_party") != std::string::npos) {
            continue;
        }

        extract_symbols(entry.path());
    }

    ready_ = true;
}

void SemanticIndex::extract_symbols(const std::filesystem::path& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) return;

    std::ostringstream content_stream;
    content_stream << file.rdbuf();
    const std::string content = content_stream.str();
    const std::string path_str = file_path.string();
    const std::string ext = file_path.extension().string();

    std::vector<SymbolInfo> symbols;

    if (ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".c") {
        symbols = extract_cpp_symbols(content, path_str);
    } else if (ext == ".py") {
        symbols = extract_python_symbols(content, path_str);
    } else if (ext == ".js" || ext == ".ts") {
        symbols = extract_js_symbols(content, path_str);
    }

    if (symbols.empty()) return;

    FileSummary summary;
    summary.path = path_str;
    summary.symbols = symbols;

    // Build summary text
    std::ostringstream summary_text;
    for (std::size_t i = 0; i < symbols.size() && i < 20; ++i) {
        if (i > 0) summary_text << ", ";
        summary_text << symbols[i].kind << " " << symbols[i].name;
    }
    summary.summary_text = summary_text.str();

    file_summaries_[path_str] = std::move(summary);
    all_symbols_.insert(all_symbols_.end(), symbols.begin(), symbols.end());
}

std::vector<SemanticIndex::SymbolInfo> SemanticIndex::extract_cpp_symbols(
    const std::string& content, const std::string& path) {
    std::vector<SymbolInfo> symbols;
    std::istringstream stream(content);
    std::string line;
    std::size_t line_num = 0;

    // Simple regex patterns for C++ symbols
    const std::regex class_re(R"(^\s*class\s+(\w+))");
    const std::regex struct_re(R"(^\s*struct\s+(\w+))");
    const std::regex func_re(R"(^[\w\s\*&:<>]+\s+(\w+)\s*\([^;]*$)");
    const std::regex method_re(R"(^\s*(?:virtual\s+)?[\w\s\*&:<>]+\s+(\w+)\s*\()");

    while (std::getline(stream, line)) {
        ++line_num;
        std::smatch match;

        if (std::regex_search(line, match, class_re)) {
            symbols.push_back({match[1].str(), "class", path, line_num});
        } else if (std::regex_search(line, match, struct_re)) {
            symbols.push_back({match[1].str(), "struct", path, line_num});
        } else if (line.find('(') != std::string::npos &&
                   line.find(';') == std::string::npos &&
                   line.find('#') == std::string::npos) {
            // Potential function definition (has parens, no semicolon, not preprocessor)
            if (std::regex_search(line, match, method_re)) {
                const std::string name = match[1].str();
                // Skip common non-function matches
                if (name != "if" && name != "for" && name != "while" &&
                    name != "switch" && name != "return" && name != "catch") {
                    symbols.push_back({name, "function", path, line_num});
                }
            }
        }
    }
    return symbols;
}

std::vector<SemanticIndex::SymbolInfo> SemanticIndex::extract_python_symbols(
    const std::string& content, const std::string& path) {
    std::vector<SymbolInfo> symbols;
    std::istringstream stream(content);
    std::string line;
    std::size_t line_num = 0;

    const std::regex class_re(R"(^class\s+(\w+))");
    const std::regex func_re(R"(^def\s+(\w+))");
    const std::regex method_re(R"(^\s+def\s+(\w+))");

    while (std::getline(stream, line)) {
        ++line_num;
        std::smatch match;

        if (std::regex_search(line, match, class_re)) {
            symbols.push_back({match[1].str(), "class", path, line_num});
        } else if (std::regex_search(line, match, func_re)) {
            symbols.push_back({match[1].str(), "function", path, line_num});
        } else if (std::regex_search(line, match, method_re)) {
            symbols.push_back({match[1].str(), "method", path, line_num});
        }
    }
    return symbols;
}

std::vector<SemanticIndex::SymbolInfo> SemanticIndex::extract_js_symbols(
    const std::string& content, const std::string& path) {
    std::vector<SymbolInfo> symbols;
    std::istringstream stream(content);
    std::string line;
    std::size_t line_num = 0;

    const std::regex class_re(R"(^\s*(?:export\s+)?class\s+(\w+))");
    const std::regex func_re(R"(^\s*(?:export\s+)?(?:async\s+)?function\s+(\w+))");
    const std::regex arrow_re(R"(^\s*(?:export\s+)?(?:const|let|var)\s+(\w+)\s*=\s*(?:async\s+)?\()");

    while (std::getline(stream, line)) {
        ++line_num;
        std::smatch match;

        if (std::regex_search(line, match, class_re)) {
            symbols.push_back({match[1].str(), "class", path, line_num});
        } else if (std::regex_search(line, match, func_re)) {
            symbols.push_back({match[1].str(), "function", path, line_num});
        } else if (std::regex_search(line, match, arrow_re)) {
            symbols.push_back({match[1].str(), "function", path, line_num});
        }
    }
    return symbols;
}

std::vector<SemanticIndex::SearchResult> SemanticIndex::search(
    const std::string& query, std::size_t max_results) const {
    if (!ready_) return {};

    std::vector<SearchResult> results;

    // Score each symbol against the query
    for (const auto& symbol : all_symbols_) {
        const auto it = file_summaries_.find(symbol.file_path);
        const std::string& summary =
            it != file_summaries_.end() ? it->second.summary_text : "";

        double relevance = compute_relevance(query, symbol.name, summary);
        if (relevance > 0.1) {
            results.push_back({symbol.file_path, symbol.line_number,
                              symbol.kind + " " + symbol.name, relevance});
        }
    }

    // Sort by relevance descending
    std::sort(results.begin(), results.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.relevance > b.relevance;
              });

    if (results.size() > max_results) {
        results.resize(max_results);
    }
    return results;
}

double SemanticIndex::compute_relevance(const std::string& query,
                                         const std::string& symbol_name,
                                         const std::string& file_summary) {
    // Case-insensitive substring match
    auto to_lower = [](const std::string& s) {
        std::string lower = s;
        for (auto& c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return lower;
    };

    const std::string q = to_lower(query);
    const std::string name = to_lower(symbol_name);
    const std::string summary = to_lower(file_summary);

    double score = 0.0;

    // Exact match
    if (name == q) return 1.0;

    // Name contains query
    if (name.find(q) != std::string::npos) {
        score = 0.8;
    }
    // Query contains name
    else if (q.find(name) != std::string::npos) {
        score = 0.6;
    }
    // File summary contains query
    else if (summary.find(q) != std::string::npos) {
        score = 0.3;
    }

    // Boost for word-boundary matches (e.g., "agent" matches "Agent" in "AgentRunner")
    // Check if query words appear at word boundaries in the name
    if (score < 0.5) {
        std::istringstream words(q);
        std::string word;
        int matches = 0;
        int total = 0;
        while (words >> word) {
            ++total;
            if (name.find(word) != std::string::npos ||
                summary.find(word) != std::string::npos) {
                ++matches;
            }
        }
        if (total > 0 && matches > 0) {
            double word_score = 0.4 * static_cast<double>(matches) / total;
            score = std::max(score, word_score);
        }
    }

    return score;
}

std::vector<SemanticIndex::SymbolInfo> SemanticIndex::symbols_in_file(
    const std::string& file_path) const {
    auto it = file_summaries_.find(file_path);
    if (it != file_summaries_.end()) {
        return it->second.symbols;
    }
    return {};
}

std::string SemanticIndex::file_summary(const std::string& file_path) const {
    auto it = file_summaries_.find(file_path);
    if (it != file_summaries_.end()) {
        return it->second.summary_text;
    }
    return "";
}

std::size_t SemanticIndex::symbol_count() const {
    return all_symbols_.size();
}
