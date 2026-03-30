#include "slack_gateway.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "../agent/agent.h"

using json = nlohmann::json;

SlackGateway::SlackGateway(std::string bot_token, std::string channel_id,
                           Agent& agent,
                           std::shared_ptr<IHttpTransport> transport,
                           std::filesystem::path workspace_root)
    : bot_token_(std::move(bot_token)),
      channel_id_(std::move(channel_id)),
      agent_(agent),
      transport_(std::move(transport)),
      workspace_root_(std::move(workspace_root)) {}

std::string SlackGateway::api_call(const std::string& method,
                                   const std::string& path,
                                   const std::string& body) {
    HttpRequest request;
    request.method = method;
    request.url = "https://slack.com/api" + path;
    request.headers.push_back({"Authorization", "Bearer " + bot_token_});
    request.headers.push_back({"Content-Type", "application/json; charset=utf-8"});
    request.body = body;
    request.timeout_ms = 10000;

    auto response = transport_->send(request);
    if (response.status_code >= 200 && response.status_code < 300) {
        return response.body;
    }
    return "";
}

void SlackGateway::send_message(const std::string& channel_id,
                                const std::string& text) {
    // Split long messages (Slack limit is 4000 chars for best display)
    const size_t MAX_LEN = 3900;

    for (size_t i = 0; i < text.size(); i += MAX_LEN) {
        std::string chunk = text.substr(i, MAX_LEN);
        json body;
        body["channel"] = channel_id;
        body["text"] = chunk;

        api_call("POST", "/chat.postMessage", body.dump());
    }
}

void SlackGateway::handle_message(const std::string& channel_id,
                                  const std::string& text) {
    // Handle commands (use ! prefix since / is reserved by Slack)
    if (text == "!start" || text == "!help") {
        send_message(channel_id,
            "*Bolt -- AI Coding Agent*\n\n"
            "Send me any coding task and I'll help!\n\n"
            "Commands:\n"
            "`!clear` -- Clear conversation history\n"
            "`!model` -- Show current model\n"
            "`!status` -- Show agent status\n"
            "`!help` -- Show this help");
        return;
    }

    if (text == "!clear") {
        agent_.clear_history();
        send_message(channel_id, "History cleared.");
        return;
    }

    if (text == "!model") {
        send_message(channel_id, "Model: `" + agent_.model() + "`");
        return;
    }

    if (text == "!status") {
        auto usage = agent_.last_token_usage();
        std::string status = "*Status*\n"
            "Model: `" + agent_.model() + "`\n"
            "Tokens: " + std::to_string(usage.input_tokens) + " in / " +
            std::to_string(usage.output_tokens) + " out";
        send_message(channel_id, status);
        return;
    }

    // Run agent turn
    try {
        std::string reply = agent_.run_turn(text);

        if (reply.empty()) {
            send_message(channel_id, "(No response)");
        } else {
            send_message(channel_id, reply);
        }
    } catch (const std::exception& e) {
        send_message(channel_id, "Error: " + std::string(e.what()));
    }
}

void SlackGateway::poll_messages() {
    std::string path = "/conversations.history?channel=" + channel_id_ + "&limit=10";
    if (!last_ts_.empty()) {
        path += "&oldest=" + last_ts_;
    }

    std::string response = api_call("GET", path);
    if (response.empty()) return;

    try {
        auto j = json::parse(response);
        if (!j.value("ok", false)) return;
        if (!j.contains("messages") || !j["messages"].is_array()) return;

        const auto& messages = j["messages"];

        // Messages come newest-first from Slack, process oldest-first
        for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
            const auto& msg = *it;
            std::string ts = msg.value("ts", "");
            std::string content = msg.value("text", "");
            std::string user = msg.value("user", "");
            std::string subtype = msg.value("subtype", "");

            // Update last timestamp
            if (!ts.empty() && ts > last_ts_) {
                last_ts_ = ts;
            }

            // Skip bot messages and own messages
            if (!subtype.empty()) continue;
            if (user == bot_user_id_) continue;

            if (!content.empty()) {
                handle_message(channel_id_, content);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Slack parse error: " << e.what() << std::endl;
    }
}

int SlackGateway::run() {
    // Verify bot token by calling auth.test
    std::string auth = api_call("POST", "/auth.test");
    if (auth.empty()) {
        std::cerr << "Error: Invalid Slack bot token or network error.\n";
        std::cerr << "Set SLACK_BOT_TOKEN environment variable.\n";
        return 1;
    }

    try {
        auto j = json::parse(auth);
        if (!j.value("ok", false)) {
            std::cerr << "Error: Slack auth failed: " << j.value("error", "unknown") << "\n";
            return 1;
        }
        std::string bot_name = j.value("user", "unknown");
        bot_user_id_ = j.value("user_id", "");
        std::cout << "Bolt Slack Gateway\n";
        std::cout << "  Bot: " << bot_name << "\n";
        std::cout << "  Channel: " << channel_id_ << "\n";
        std::cout << "  Model: " << agent_.model() << "\n";
        std::cout << "  Workspace: " << workspace_root_.string() << "\n";
        std::cout << "  Listening for messages... (Ctrl+C to stop)\n\n";
    } catch (...) {}

    // Polling loop
    while (running_) {
        try {
            poll_messages();
        } catch (const std::exception& e) {
            std::cerr << "Poll error: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    return 0;
}

void SlackGateway::stop() {
    running_ = false;
}
