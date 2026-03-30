#include "telegram_gateway.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "../agent/agent.h"

using json = nlohmann::json;

TelegramGateway::TelegramGateway(std::string bot_token, Agent& agent,
                                 std::shared_ptr<IHttpTransport> transport,
                                 std::filesystem::path workspace_root)
    : bot_token_(std::move(bot_token)),
      agent_(agent),
      transport_(std::move(transport)),
      workspace_root_(std::move(workspace_root)) {}

std::string TelegramGateway::api_url(const std::string& method) const {
    return "https://api.telegram.org/bot" + bot_token_ + "/" + method;
}

std::string TelegramGateway::call_api(const std::string& method, const std::string& body) {
    HttpRequest request;
    request.method = body.empty() ? "GET" : "POST";
    request.url = api_url(method);
    request.body = body;
    if (!body.empty()) {
        request.headers.push_back({"Content-Type", "application/json"});
    }
    request.timeout_ms = 35000;  // Long polling timeout + margin

    auto response = transport_->send(request);
    if (response.status_code != 200) return "";
    return response.body;
}

void TelegramGateway::send_message(int chat_id, const std::string& text) {
    // Split long messages (Telegram limit is 4096 chars)
    const size_t MAX_LEN = 4000;

    for (size_t i = 0; i < text.size(); i += MAX_LEN) {
        std::string chunk = text.substr(i, MAX_LEN);
        json body;
        body["chat_id"] = chat_id;
        body["text"] = chunk;
        body["parse_mode"] = "Markdown";

        auto result = call_api("sendMessage", body.dump());
        if (result.empty()) {
            // Retry without Markdown (some formatting may fail)
            body.erase("parse_mode");
            call_api("sendMessage", body.dump());
        }
    }
}

void TelegramGateway::send_typing(int chat_id) {
    json body;
    body["chat_id"] = chat_id;
    body["action"] = "typing";
    call_api("sendChatAction", body.dump());
}

void TelegramGateway::handle_message(int chat_id, const std::string& text) {
    // Handle /start command
    if (text == "/start") {
        send_message(chat_id,
            "*Bolt — AI Coding Agent*\n\n"
            "Send me any coding task and I'll help!\n\n"
            "Commands:\n"
            "/clear — Clear conversation history\n"
            "/model — Show current model\n"
            "/status — Show agent status\n"
            "/help — Show this help");
        return;
    }

    if (text == "/clear") {
        agent_.clear_history();
        send_message(chat_id, "History cleared.");
        return;
    }

    if (text == "/model") {
        send_message(chat_id, "Model: `" + agent_.model() + "`");
        return;
    }

    if (text == "/status") {
        auto usage = agent_.last_token_usage();
        std::string status = "*Status*\n"
            "Model: `" + agent_.model() + "`\n"
            "Tokens: " + std::to_string(usage.input_tokens) + " in / " +
            std::to_string(usage.output_tokens) + " out";
        send_message(chat_id, status);
        return;
    }

    if (text == "/help") {
        handle_message(chat_id, "/start");
        return;
    }

    // Send typing indicator
    send_typing(chat_id);

    // Run agent turn
    try {
        std::string reply = agent_.run_turn(text);

        if (reply.empty()) {
            send_message(chat_id, "(No response)");
        } else {
            send_message(chat_id, reply);
        }
    } catch (const std::exception& e) {
        send_message(chat_id, "Error: " + std::string(e.what()));
    }
}

void TelegramGateway::poll_updates() {
    json params;
    params["offset"] = last_update_id_ + 1;
    params["timeout"] = 30;  // Long polling: wait up to 30 seconds
    params["allowed_updates"] = json::array({"message"});

    std::string response = call_api("getUpdates", params.dump());
    if (response.empty()) return;

    try {
        auto j = json::parse(response);
        if (!j.value("ok", false)) return;
        if (!j.contains("result") || !j["result"].is_array()) return;

        for (const auto& update : j["result"]) {
            int update_id = update.value("update_id", 0);
            if (update_id > last_update_id_) {
                last_update_id_ = update_id;
            }

            if (!update.contains("message")) continue;
            const auto& msg = update["message"];

            if (!msg.contains("chat") || !msg.contains("text")) continue;

            int chat_id = msg["chat"].value("id", 0);
            std::string msg_text = msg.value("text", "");

            if (chat_id > 0 && !msg_text.empty()) {
                handle_message(chat_id, msg_text);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Telegram parse error: " << e.what() << std::endl;
    }
}

int TelegramGateway::run() {
    // Verify bot token by calling getMe
    std::string me = call_api("getMe");
    if (me.empty()) {
        std::cerr << "Error: Invalid Telegram bot token or network error.\n";
        std::cerr << "Set TELEGRAM_BOT_TOKEN environment variable.\n";
        return 1;
    }

    try {
        auto j = json::parse(me);
        if (j.value("ok", false) && j.contains("result")) {
            std::string bot_name = j["result"].value("username", "unknown");
            std::cout << "Bolt Telegram Gateway\n";
            std::cout << "  Bot: @" << bot_name << "\n";
            std::cout << "  Model: " << agent_.model() << "\n";
            std::cout << "  Workspace: " << workspace_root_.string() << "\n";
            std::cout << "  Listening for messages... (Ctrl+C to stop)\n\n";
        }
    } catch (...) {}

    // Polling loop
    while (running_) {
        try {
            poll_updates();
        } catch (const std::exception& e) {
            std::cerr << "Poll error: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    return 0;
}

void TelegramGateway::stop() {
    running_ = false;
}
