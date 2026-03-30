#include "web_search_tool.h"

#include <algorithm>
#include <sstream>

#include "workspace_utils.h"

namespace {

constexpr int kTimeoutMs = 15000;
constexpr std::size_t kMaxResults = 10;

std::string decode_entities(const std::string& text) {
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
            } else if (text.compare(i, 5, "&#39;") == 0) {
                result += '\'';
                i += 4;
            } else {
                result += text[i];
            }
        } else {
            result += text[i];
        }
    }
    return result;
}

}  // namespace

WebSearchTool::WebSearchTool(std::shared_ptr<IHttpTransport> transport)
    : transport_(std::move(transport)) {}

std::string WebSearchTool::name() const {
    return "web_search";
}

std::string WebSearchTool::description() const {
    return "Search the web. Returns search result titles, URLs, and snippets.";
}

ToolSchema WebSearchTool::schema() const {
    return {name(), description(), {
        {"query", "string", "Search query", true},
    }};
}

ToolResult WebSearchTool::run(const std::string& args) const {
    try {
        std::string query = extract_query(args);
        if (query.empty()) {
            return {false, "Expected a search query"};
        }

        std::string encoded_query = url_encode(query);
        std::string url = "https://html.duckduckgo.com/html/?q=" + encoded_query;

        HttpRequest request;
        request.method = "GET";
        request.url = url;
        request.timeout_ms = kTimeoutMs;
        request.headers = {{"User-Agent", "Bolt/1.0"}};

        HttpResponse response = transport_->send(request);

        if (!response.error.empty()) {
            return {false, "Search error: " + response.error};
        }
        if (response.status_code < 200 || response.status_code >= 400) {
            return {false, "Search returned HTTP " + std::to_string(response.status_code)};
        }

        auto results = parse_duckduckgo_html(response.body);

        if (results.empty()) {
            return {true, "No search results found for: " + query};
        }

        std::ostringstream output;
        for (std::size_t i = 0; i < results.size(); ++i) {
            output << (i + 1) << ". " << results[i].title << "\n";
            output << "   " << results[i].url << "\n";
            if (!results[i].snippet.empty()) {
                output << "   " << results[i].snippet << "\n";
            }
            output << "\n";
        }

        return {true, output.str()};
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
}

std::vector<WebSearchTool::SearchResult> WebSearchTool::parse_duckduckgo_html(
    const std::string& html) const {
    std::vector<SearchResult> results;

    // DuckDuckGo HTML results contain:
    //   <a class="result__a" href="...">Title</a>
    //   <a class="result__snippet" href="...">Snippet text</a>
    // We parse these by finding the class markers.

    const std::string title_marker = "class=\"result__a\"";
    const std::string snippet_marker = "class=\"result__snippet\"";

    std::size_t pos = 0;
    while (pos < html.size() && results.size() < kMaxResults) {
        // Find next title link
        auto title_pos = html.find(title_marker, pos);
        if (title_pos == std::string::npos) break;

        SearchResult result;

        // Extract href from the title anchor tag
        // Search backwards from the marker to find href="..."
        auto tag_start = html.rfind('<', title_pos);
        if (tag_start != std::string::npos) {
            auto href_pos = html.find("href=\"", tag_start);
            if (href_pos != std::string::npos && href_pos < title_pos + title_marker.size() + 50) {
                href_pos += 6;  // skip href="
                auto href_end = html.find('"', href_pos);
                if (href_end != std::string::npos) {
                    result.url = html.substr(href_pos, href_end - href_pos);
                    // DuckDuckGo may use redirect URLs like //duckduckgo.com/l/?uddg=...
                    // Try to extract the actual URL from uddg parameter
                    auto uddg_pos = result.url.find("uddg=");
                    if (uddg_pos != std::string::npos) {
                        std::string encoded_url = result.url.substr(uddg_pos + 5);
                        auto amp_pos = encoded_url.find('&');
                        if (amp_pos != std::string::npos) {
                            encoded_url = encoded_url.substr(0, amp_pos);
                        }
                        // Basic URL decode for the redirect URL
                        std::string decoded;
                        for (std::size_t i = 0; i < encoded_url.size(); ++i) {
                            if (encoded_url[i] == '%' && i + 2 < encoded_url.size()) {
                                int hex_val = 0;
                                std::string hex_str = encoded_url.substr(i + 1, 2);
                                try {
                                    hex_val = std::stoi(hex_str, nullptr, 16);
                                    decoded += static_cast<char>(hex_val);
                                    i += 2;
                                } catch (...) {
                                    decoded += encoded_url[i];
                                }
                            } else if (encoded_url[i] == '+') {
                                decoded += ' ';
                            } else {
                                decoded += encoded_url[i];
                            }
                        }
                        if (!decoded.empty()) {
                            result.url = decoded;
                        }
                    }
                }
            }
        }

        // Extract title text (content between > and </a>)
        auto title_tag_end = html.find('>', title_pos);
        if (title_tag_end != std::string::npos) {
            auto title_close = html.find("</a>", title_tag_end);
            if (title_close != std::string::npos) {
                std::string title_html = html.substr(title_tag_end + 1,
                                                      title_close - title_tag_end - 1);
                result.title = decode_entities(strip_tags(title_html));
            }
        }

        pos = title_pos + title_marker.size();

        // Look for snippet after this title, before next title
        auto next_title = html.find(title_marker, pos);
        auto snippet_pos = html.find(snippet_marker, pos);
        if (snippet_pos != std::string::npos &&
            (next_title == std::string::npos || snippet_pos < next_title)) {
            auto snippet_tag_end = html.find('>', snippet_pos);
            if (snippet_tag_end != std::string::npos) {
                auto snippet_close = html.find("</a>", snippet_tag_end);
                if (snippet_close == std::string::npos) {
                    snippet_close = html.find("</span>", snippet_tag_end);
                }
                if (snippet_close != std::string::npos) {
                    std::string snippet_html = html.substr(snippet_tag_end + 1,
                                                            snippet_close - snippet_tag_end - 1);
                    result.snippet = decode_entities(strip_tags(snippet_html));
                }
            }
        }

        if (!result.title.empty() || !result.url.empty()) {
            results.push_back(std::move(result));
        }
    }

    return results;
}

std::string WebSearchTool::url_encode(const std::string& value) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex;

    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else if (c == ' ') {
            encoded << '+';
        } else {
            encoded << '%' << std::uppercase;
            if (c < 16) encoded << '0';
            encoded << static_cast<int>(c);
            encoded << std::nouppercase;
        }
    }

    return encoded.str();
}

std::string WebSearchTool::strip_tags(const std::string& html) {
    std::string result;
    result.reserve(html.size());
    bool in_tag = false;
    for (char c : html) {
        if (c == '<') {
            in_tag = true;
        } else if (c == '>') {
            in_tag = false;
        } else if (!in_tag) {
            result += c;
        }
    }
    return result;
}

std::string WebSearchTool::extract_query(const std::string& args) {
    std::string trimmed = trim_copy(args);

    // Check for key=value format: query=<value>
    if (trimmed.find("query=") != std::string::npos) {
        auto pos = trimmed.find("query=");
        auto value = trimmed.substr(pos + 6);
        auto end_pos = value.find('\n');
        if (end_pos != std::string::npos) {
            value = value.substr(0, end_pos);
        }
        return trim_copy(value);
    }

    // Otherwise treat the whole args as the query
    return trimmed;
}
