#include "wechat_gateway.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "../agent/agent.h"

using json = nlohmann::json;

WeChatGateway::WeChatGateway(std::string webhook_url, Agent& agent,
                             std::shared_ptr<IHttpTransport> transport,
                             std::filesystem::path workspace_root)
    : webhook_url_(std::move(webhook_url)),
      agent_(agent),
      transport_(std::move(transport)),
      workspace_root_(std::move(workspace_root)) {}

void WeChatGateway::send_message(const std::string& to, const std::string& text) {
    const size_t MAX_LEN = 2000;

    for (size_t i = 0; i < text.size(); i += MAX_LEN) {
        json body;
        body["to"] = to;
        body["content"] = text.substr(i, MAX_LEN);

        HttpRequest req;
        req.method = "POST";
        req.url = webhook_url_ + "/api/sendTextMsg";
        req.headers.push_back({"Content-Type", "application/json"});
        req.body = body.dump();
        req.timeout_ms = 10000;
        transport_->send(req);
    }
}

void WeChatGateway::handle_message(const std::string& from, const std::string& content) {
    // Handle commands
    if (content == "/clear") {
        agent_.clear_history();
        send_message(from, "History cleared.");
        return;
    }

    if (content == "/model") {
        send_message(from, "Model: " + agent_.model());
        return;
    }

    if (content == "/help") {
        send_message(from,
            "Bolt AI Assistant\n\n"
            "Send any message and I will help!\n\n"
            "Commands:\n"
            "/clear - Clear conversation history\n"
            "/model - Show current model\n"
            "/status - Show status\n"
            "/help - Show this help");
        return;
    }

    if (content == "/status") {
        auto usage = agent_.last_token_usage();
        std::string status = "Status\n"
            "Model: " + agent_.model() + "\n"
            "Tokens: " + std::to_string(usage.input_tokens) + " in / " +
            std::to_string(usage.output_tokens) + " out";
        send_message(from, status);
        return;
    }

    // Run agent turn
    try {
        std::string reply = agent_.run_turn(content);

        if (reply.empty()) {
            send_message(from, "(No response)");
        } else {
            send_message(from, reply);
        }
    } catch (const std::exception& e) {
        send_message(from, "Error: " + std::string(e.what()));
    }
}

void WeChatGateway::poll_messages() {
    HttpRequest req;
    req.method = "GET";
    req.url = webhook_url_ + "/api/messages";
    req.timeout_ms = 10000;

    auto resp = transport_->send(req);
    if (resp.status_code != 200 || resp.body.empty()) return;

    try {
        auto j = json::parse(resp.body);
        if (!j.is_array()) return;

        for (const auto& msg : j) {
            std::string from = msg.value("from", "");
            std::string msg_content = msg.value("content", "");
            bool is_self = msg.value("isSelf", false);

            if (!is_self && !from.empty() && !msg_content.empty()) {
                handle_message(from, msg_content);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "WeChat parse error: " << e.what() << std::endl;
    }
}

int WeChatGateway::run() {
    // Test connection
    HttpRequest req;
    req.method = "GET";
    req.url = webhook_url_ + "/api/status";
    req.timeout_ms = 5000;
    auto resp = transport_->send(req);

    if (resp.status_code != 200) {
        std::cerr << "Error: Cannot connect to WeChat webhook at " << webhook_url_ << "\n";
        std::cerr << "Start wechatbot-webhook first: npx wechatbot-webhook\n";
        return 1;
    }

    std::cout << "Bolt WeChat Gateway\n";
    std::cout << "  Webhook: " << webhook_url_ << "\n";
    std::cout << "  Model: " << agent_.model() << "\n";
    std::cout << "  Workspace: " << workspace_root_.string() << "\n";
    std::cout << "  Listening for messages... (Ctrl+C to stop)\n\n";

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

void WeChatGateway::stop() {
    running_ = false;
}
