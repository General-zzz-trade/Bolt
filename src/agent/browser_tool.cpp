#include "browser_tool.h"

#include <algorithm>
#include <sstream>

namespace {

constexpr std::size_t kMaxOutputBytes = 32 * 1024;  // 32KB
constexpr std::size_t kCommandTimeoutMs = 30000;     // 30s

std::string decode_html_entities(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '&') {
            if (text.compare(i, 5, "&amp;") == 0) {
                result += '&';
                i += 4;
            } else if (text.compare(i, 4, "&lt;") == 0) {
                result += '<';
                i += 3;
            } else if (text.compare(i, 4, "&gt;") == 0) {
                result += '>';
                i += 3;
            } else if (text.compare(i, 6, "&quot;") == 0) {
                result += '"';
                i += 5;
            } else if (text.compare(i, 6, "&nbsp;") == 0) {
                result += ' ';
                i += 5;
            } else if (text.compare(i, 6, "&apos;") == 0) {
                result += '\'';
                i += 5;
            } else {
                result += text[i];
            }
        } else {
            result += text[i];
        }
    }
    return result;
}

std::string collapse_whitespace(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    bool last_was_space = false;
    bool last_was_newline = false;
    for (char c : text) {
        if (c == '\n' || c == '\r') {
            if (!last_was_newline) {
                result += '\n';
                last_was_newline = true;
            }
            last_was_space = false;
        } else if (c == ' ' || c == '\t') {
            if (!last_was_space && !last_was_newline) {
                result += ' ';
                last_was_space = true;
            }
        } else {
            result += c;
            last_was_space = false;
            last_was_newline = false;
        }
    }
    return result;
}

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

// Shell-escape a string by wrapping in single quotes
std::string shell_escape(const std::string& s) {
    std::string result = "'";
    for (char c : s) {
        if (c == '\'') {
            result += "'\\''";
        } else {
            result += c;
        }
    }
    result += "'";
    return result;
}

}  // namespace

BrowserTool::BrowserTool(std::filesystem::path workspace_root,
                         std::shared_ptr<ICommandRunner> command_runner)
    : workspace_root_(std::move(workspace_root))
    , command_runner_(std::move(command_runner)) {}

std::string BrowserTool::name() const { return "browser"; }

std::string BrowserTool::description() const {
    return "Browse web pages using a headless browser. Commands: "
           "navigate <url> (get page text), "
           "screenshot <url> [file] (capture screenshot), "
           "extract <url> (extract main content text). "
           "Requires Chrome/Chromium installed.";
}

ToolSchema BrowserTool::schema() const {
    return {name(), description(), {
        {"command", "string",
         "Browser command: 'navigate <url>', 'screenshot <url> [output.png]', 'extract <url>'",
         true},
    }};
}

std::string BrowserTool::find_chrome_binary() {
    const char* candidates[] = {
        "/usr/bin/chromium-browser",
        "/usr/bin/chromium",
        "/usr/bin/google-chrome",
        "/usr/bin/google-chrome-stable",
        "/snap/bin/chromium",
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Chromium.app/Contents/MacOS/Chromium",
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) return path;
    }
    // Fallback: let the shell resolve it via PATH
    return "chromium";
}

ToolResult BrowserTool::navigate(const std::string& url) const {
    const std::string chrome = find_chrome_binary();

    std::string cmd = shell_escape(chrome)
        + " --headless=new"
          " --disable-gpu"
          " --no-sandbox"
          " --disable-dev-shm-usage"
          " --disable-extensions"
          " --disable-background-networking"
          " --timeout=15000"
          " --dump-dom"
          " " + shell_escape(url)
        + " 2>/dev/null";

    auto result = command_runner_->run(cmd, workspace_root_, kCommandTimeoutMs);

    if (!result.success && result.stdout_output.empty()) {
        // Try alternative binary name
        cmd = "chromium-browser --headless=new --disable-gpu --no-sandbox --dump-dom "
              + shell_escape(url) + " 2>/dev/null";
        result = command_runner_->run(cmd, workspace_root_, kCommandTimeoutMs);
    }

    if (!result.success && result.stdout_output.empty()) {
        return {false, "Chrome/Chromium not found or failed to load page. "
                       "Install: sudo apt install chromium-browser\n"
                       + result.stderr_output};
    }

    std::string text = html_to_text(result.stdout_output);

    if (text.size() > kMaxOutputBytes) {
        text.resize(kMaxOutputBytes);
        text += "\n... [truncated]";
    }

    return {true, text};
}

ToolResult BrowserTool::screenshot(const std::string& url,
                                   const std::string& output_path) const {
    const std::string chrome = find_chrome_binary();
    const std::string output = output_path.empty()
        ? (workspace_root_ / "screenshot.png").string()
        : (workspace_root_ / output_path).string();

    std::string cmd = shell_escape(chrome)
        + " --headless=new"
          " --disable-gpu"
          " --no-sandbox"
          " --disable-dev-shm-usage"
          " --window-size=1280,720"
          " --screenshot=" + shell_escape(output)
        + " " + shell_escape(url)
        + " 2>/dev/null";

    auto result = command_runner_->run(cmd, workspace_root_, kCommandTimeoutMs);

    if (std::filesystem::exists(output)) {
        auto size = std::filesystem::file_size(output);
        return {true, "Screenshot saved: " + output + " (" + std::to_string(size) + " bytes)"};
    }

    return {false, "Failed to capture screenshot. " + result.stderr_output};
}

ToolResult BrowserTool::extract_text(const std::string& url) const {
    return navigate(url);
}

ToolResult BrowserTool::run(const std::string& args) const {
    std::string trimmed = trim(args);
    if (trimmed.empty()) {
        return {false, "Empty browser command"};
    }

    // Try to parse JSON args: extract "command" value
    if (trimmed.front() == '{') {
        auto pos = trimmed.find("\"command\"");
        if (pos != std::string::npos) {
            auto colon = trimmed.find(':', pos);
            if (colon != std::string::npos) {
                auto q1 = trimmed.find('"', colon + 1);
                auto q2 = trimmed.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    trimmed = trimmed.substr(q1 + 1, q2 - q1 - 1);
                }
            }
        }
    }

    auto space = trimmed.find(' ');
    if (space == std::string::npos) {
        // Maybe it's just a URL
        if (trimmed.find("http") == 0 || trimmed.find("www.") == 0) {
            return navigate(trimmed);
        }
        return {false, "Usage: navigate <url>, screenshot <url> [file], extract <url>"};
    }

    std::string cmd = trimmed.substr(0, space);
    std::string rest = trim(trimmed.substr(space + 1));

    if (cmd == "navigate" || cmd == "open" || cmd == "goto") {
        return navigate(rest);
    }
    if (cmd == "screenshot" || cmd == "capture") {
        auto sp = rest.find(' ');
        if (sp != std::string::npos) {
            std::string url_part = rest.substr(0, sp);
            std::string file_part = trim(rest.substr(sp + 1));
            return screenshot(url_part, file_part);
        }
        return screenshot(rest, "");
    }
    if (cmd == "extract" || cmd == "text") {
        return extract_text(rest);
    }

    // Default: treat entire string as a URL
    return navigate(trimmed);
}

std::string BrowserTool::html_to_text(const std::string& html) {
    std::string result;
    result.reserve(html.size());

    bool in_tag = false;
    bool in_script = false;
    bool in_style = false;

    for (std::size_t i = 0; i < html.size(); ++i) {
        if (html[i] == '<') {
            std::string lower_ahead;
            std::size_t ahead_len = std::min(std::size_t(16), html.size() - i);
            for (std::size_t j = 0; j < ahead_len; ++j) {
                lower_ahead += static_cast<char>(::tolower(html[i + j]));
            }

            if (lower_ahead.compare(0, 7, "<script") == 0) {
                in_script = true;
            } else if (lower_ahead.compare(0, 9, "</script>") == 0) {
                in_script = false;
                auto end_pos = html.find('>', i);
                if (end_pos != std::string::npos) {
                    i = end_pos;
                }
                continue;
            } else if (lower_ahead.compare(0, 6, "<style") == 0) {
                in_style = true;
            } else if (lower_ahead.compare(0, 8, "</style>") == 0) {
                in_style = false;
                auto end_pos = html.find('>', i);
                if (end_pos != std::string::npos) {
                    i = end_pos;
                }
                continue;
            }

            if (!in_script && !in_style) {
                if (lower_ahead.compare(0, 2, "<p") == 0 ||
                    lower_ahead.compare(0, 3, "<br") == 0 ||
                    lower_ahead.compare(0, 4, "<div") == 0 ||
                    lower_ahead.compare(0, 3, "<li") == 0 ||
                    lower_ahead.compare(0, 3, "<h1") == 0 ||
                    lower_ahead.compare(0, 3, "<h2") == 0 ||
                    lower_ahead.compare(0, 3, "<h3") == 0 ||
                    lower_ahead.compare(0, 3, "<h4") == 0 ||
                    lower_ahead.compare(0, 3, "<h5") == 0 ||
                    lower_ahead.compare(0, 3, "<h6") == 0 ||
                    lower_ahead.compare(0, 3, "<tr") == 0) {
                    result += '\n';
                }
            }

            in_tag = true;
        } else if (html[i] == '>') {
            in_tag = false;
        } else if (!in_tag && !in_script && !in_style) {
            result += html[i];
        }
    }

    result = decode_html_entities(result);
    result = collapse_whitespace(result);

    auto start = result.find_first_not_of(" \t\n\r");
    auto end = result.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    return result.substr(start, end - start + 1);
}
