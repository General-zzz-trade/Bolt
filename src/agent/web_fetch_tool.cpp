#include "web_fetch_tool.h"

#include <algorithm>
#include <sstream>

#include "workspace_utils.h"

namespace {

constexpr int kTimeoutMs = 15000;
constexpr std::size_t kMaxResponseBytes = 1024 * 1024;  // 1MB
constexpr std::size_t kMaxOutputBytes = 32 * 1024;       // 32KB

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
            } else if (text.compare(i, 5, "&amp;") == 0) {
                result += '&';
                i += 4;
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

}  // namespace

WebFetchTool::WebFetchTool(std::shared_ptr<IHttpTransport> transport)
    : transport_(std::move(transport)) {}

std::string WebFetchTool::name() const {
    return "web_fetch";
}

std::string WebFetchTool::description() const {
    return "Fetch content from a URL. Returns the page text.";
}

ToolSchema WebFetchTool::schema() const {
    return {name(), description(), {
        {"url", "string", "URL to fetch", true},
    }};
}

ToolResult WebFetchTool::run(const std::string& args) const {
    try {
        std::string url = extract_url(args);
        if (url.empty()) {
            return {false, "Expected a URL parameter"};
        }

        HttpRequest request;
        request.method = "GET";
        request.url = url;
        request.timeout_ms = kTimeoutMs;
        request.headers = {{"User-Agent", "Bolt/1.0"}};

        HttpResponse response = transport_->send(request);

        if (!response.error.empty()) {
            return {false, "HTTP error: " + response.error};
        }
        if (response.status_code < 200 || response.status_code >= 400) {
            return {false, "HTTP " + std::to_string(response.status_code)};
        }

        // Truncate to max response size
        std::string body = response.body;
        if (body.size() > kMaxResponseBytes) {
            body.resize(kMaxResponseBytes);
        }

        // Convert HTML to text if the response looks like HTML
        bool is_html = false;
        {
            std::string lower_body = body.substr(0, std::min(body.size(), std::size_t(1024)));
            std::transform(lower_body.begin(), lower_body.end(), lower_body.begin(), ::tolower);
            if (lower_body.find("<html") != std::string::npos ||
                lower_body.find("<!doctype") != std::string::npos ||
                lower_body.find("<head") != std::string::npos) {
                is_html = true;
            }
        }

        std::string text = is_html ? html_to_text(body) : body;

        // Truncate output
        if (text.size() > kMaxOutputBytes) {
            text.resize(kMaxOutputBytes);
            text += "\n[truncated]";
        }

        return {true, text};
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
}

std::string WebFetchTool::html_to_text(const std::string& html) {
    std::string result;
    result.reserve(html.size());

    bool in_tag = false;
    bool in_script = false;
    bool in_style = false;

    for (std::size_t i = 0; i < html.size(); ++i) {
        if (html[i] == '<') {
            // Check for script/style open/close tags
            std::string lower_ahead;
            std::size_t ahead_len = std::min(std::size_t(16), html.size() - i);
            for (std::size_t j = 0; j < ahead_len; ++j) {
                lower_ahead += static_cast<char>(::tolower(html[i + j]));
            }

            if (lower_ahead.compare(0, 7, "<script") == 0) {
                in_script = true;
            } else if (lower_ahead.compare(0, 9, "</script>") == 0) {
                in_script = false;
                // Skip past the closing tag
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

            // Add newline for block elements
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

    // Trim leading/trailing whitespace
    auto start = result.find_first_not_of(" \t\n\r");
    auto end = result.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    return result.substr(start, end - start + 1);
}

std::string WebFetchTool::extract_url(const std::string& args) {
    std::string trimmed = trim_copy(args);

    // Check for key=value format: url=<value>
    if (trimmed.find("url=") != std::string::npos) {
        auto pos = trimmed.find("url=");
        auto value = trimmed.substr(pos + 4);
        // Value ends at newline or end of string
        auto end_pos = value.find('\n');
        if (end_pos != std::string::npos) {
            value = value.substr(0, end_pos);
        }
        return trim_copy(value);
    }

    // If args looks like a URL, use it directly
    if (trimmed.rfind("http://", 0) == 0 || trimmed.rfind("https://", 0) == 0) {
        return trimmed;
    }

    return "";
}
