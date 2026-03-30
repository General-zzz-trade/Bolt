#include "mcp_server.h"

#include <iostream>
#include <sstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

void McpServer::register_tool(ToolHandler handler) {
    tools_.push_back(std::move(handler));
}

int McpServer::run(std::istream& input, std::ostream& output) {
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) continue;

        const std::string response = handle_request(line);
        if (!response.empty()) {
            output << response << "\n" << std::flush;
        }

        // Check for shutdown
        try {
            auto j = json::parse(line);
            if (j.value("method", "") == "shutdown") {
                return 0;
            }
        } catch (...) {}
    }
    return 0;
}

std::string McpServer::handle_request(const std::string& json_request) {
    try {
        auto j = json::parse(json_request);
        const std::string method = j.value("method", "");
        const std::string id = j.contains("id") ? j["id"].dump() : "null";

        // === Lifecycle ===
        if (method == "initialize") {
            return handle_initialize(id);
        }
        if (method == "shutdown") {
            json resp;
            resp["jsonrpc"] = "2.0";
            resp["id"] = json::parse(id);
            resp["result"] = nullptr;
            return resp.dump();
        }

        // === Notifications (no id, no response expected) ===
        if (method == "notifications/initialized" ||
            method == "notifications/cancelled" ||
            method == "notifications/progress" ||
            method == "notifications/roots/list_changed") {
            // Acknowledged silently — no response for notifications
            return "";
        }

        // === Tools ===
        if (method == "tools/list") {
            return handle_tools_list(id);
        }
        if (method == "tools/call") {
            const std::string name = j["params"]["name"].get<std::string>();
            const std::string args = j["params"].contains("arguments")
                ? j["params"]["arguments"].dump() : "{}";
            return handle_tools_call(id, name, args);
        }

        // === Resources (stub — returns empty list) ===
        if (method == "resources/list") {
            json resp;
            resp["jsonrpc"] = "2.0";
            resp["id"] = json::parse(id);
            resp["result"] = {{"resources", json::array()}};
            return resp.dump();
        }
        if (method == "resources/templates/list") {
            json resp;
            resp["jsonrpc"] = "2.0";
            resp["id"] = json::parse(id);
            resp["result"] = {{"resourceTemplates", json::array()}};
            return resp.dump();
        }

        // === Prompts (stub — returns empty list) ===
        if (method == "prompts/list") {
            json resp;
            resp["jsonrpc"] = "2.0";
            resp["id"] = json::parse(id);
            resp["result"] = {{"prompts", json::array()}};
            return resp.dump();
        }

        // === Logging ===
        if (method == "logging/setLevel") {
            // Accept but ignore — we log to stderr
            json resp;
            resp["jsonrpc"] = "2.0";
            resp["id"] = json::parse(id);
            resp["result"] = json::object();
            return resp.dump();
        }

        // === Ping ===
        if (method == "ping") {
            json resp;
            resp["jsonrpc"] = "2.0";
            resp["id"] = json::parse(id);
            resp["result"] = json::object();
            return resp.dump();
        }

        // Unknown notifications (no id) don't need response
        if (!j.contains("id")) return "";

        return make_error(id, -32601, "Method not found: " + method);
    } catch (const std::exception& e) {
        return make_error("null", -32700, std::string("Parse error: ") + e.what());
    }
}

std::string McpServer::handle_initialize(const std::string& id) {
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = json::parse(id);
    resp["result"] = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", {
            {"tools", {{"listChanged", false}}},
            {"resources", {{"subscribe", false}, {"listChanged", false}}},
            {"prompts", {{"listChanged", false}}},
            {"logging", json::object()}
        }},
        {"serverInfo", {
            {"name", "bolt"},
            {"version", "0.4.0"}
        }}
    };
    return resp.dump();
}

std::string McpServer::handle_tools_list(const std::string& id) {
    json tools_arr = json::array();
    for (const auto& tool : tools_) {
        json params = json::object();
        std::vector<std::string> required;
        for (const auto& p : tool.schema.parameters) {
            params[p.name] = {{"type", p.type}, {"description", p.description}};
            if (p.required) required.push_back(p.name);
        }

        tools_arr.push_back({
            {"name", tool.name},
            {"description", tool.description},
            {"inputSchema", {
                {"type", "object"},
                {"properties", params},
                {"required", required}
            }}
        });
    }

    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = json::parse(id);
    resp["result"] = {{"tools", tools_arr}};
    return resp.dump();
}

std::string McpServer::handle_tools_call(const std::string& id,
                                          const std::string& tool_name,
                                          const std::string& arguments) {
    for (const auto& tool : tools_) {
        if (tool.name == tool_name) {
            try {
                // Convert JSON arguments to the format tools expect
                std::string tool_args = arguments;
                try {
                    auto j = json::parse(arguments);
                    // Tools with JSON-native parsers: pass raw JSON
                    if (tool_name == "edit_file" || tool_name == "write_file" ||
                        tool_name == "send_email" || tool_name == "browser") {
                        tool_args = arguments;
                    } else if (j.contains("args") && j["args"].is_string()) {
                        tool_args = j["args"].get<std::string>();
                    } else if (j.contains("command") && j["command"].is_string()) {
                        tool_args = j["command"].get<std::string>();
                    } else if (j.contains("query") && j["query"].is_string()) {
                        // search_code: convert {"query":"x","mode":"text"} to "query=x\nmode=text"
                        std::string result_args;
                        for (auto it = j.begin(); it != j.end(); ++it) {
                            if (!result_args.empty()) result_args += "\n";
                            result_args += it.key() + "=" +
                                (it->is_string() ? it->get<std::string>() : it->dump());
                        }
                        tool_args = result_args;
                    } else if (j.contains("path") && j["path"].is_string() && j.size() == 1) {
                        tool_args = j["path"].get<std::string>();
                    } else if (j.size() == 1 && j.begin()->is_string()) {
                        tool_args = j.begin()->get<std::string>();
                    }
                    // else: pass raw JSON, let tool parse it
                } catch (...) {
                    // Not valid JSON, pass as-is
                }

                const std::string result = tool.execute(tool_args);
                json resp;
                resp["jsonrpc"] = "2.0";
                resp["id"] = json::parse(id);
                resp["result"] = {
                    {"content", json::array({{{"type", "text"}, {"text", result}}})}
                };
                return resp.dump();
            } catch (const std::exception& e) {
                json resp;
                resp["jsonrpc"] = "2.0";
                resp["id"] = json::parse(id);
                resp["result"] = {
                    {"content", json::array({{{"type", "text"}, {"text", e.what()}}})},
                    {"isError", true}
                };
                return resp.dump();
            }
        }
    }
    return make_error(id, -32602, "Unknown tool: " + tool_name);
}

std::string McpServer::make_error(const std::string& id, int code, const std::string& message) {
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = json::parse(id);
    resp["error"] = {{"code", code}, {"message", message}};
    return resp.dump();
}
