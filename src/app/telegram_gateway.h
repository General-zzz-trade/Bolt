#ifndef APP_TELEGRAM_GATEWAY_H
#define APP_TELEGRAM_GATEWAY_H

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

#include "../core/interfaces/http_transport.h"

class Agent;

class TelegramGateway {
public:
    TelegramGateway(std::string bot_token, Agent& agent,
                    std::shared_ptr<IHttpTransport> transport,
                    std::filesystem::path workspace_root);

    int run();   // Blocking: runs the polling loop
    void stop();

private:
    std::string bot_token_;
    Agent& agent_;
    std::shared_ptr<IHttpTransport> transport_;
    std::filesystem::path workspace_root_;
    std::atomic<bool> running_{true};
    int last_update_id_ = 0;

    // Telegram API helpers
    std::string api_url(const std::string& method) const;
    std::string call_api(const std::string& method, const std::string& body = "");

    // Message handling
    void poll_updates();
    void handle_message(int chat_id, const std::string& text);
    void send_message(int chat_id, const std::string& text);
    void send_typing(int chat_id);
};

#endif
