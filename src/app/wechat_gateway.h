#ifndef APP_WECHAT_GATEWAY_H
#define APP_WECHAT_GATEWAY_H

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

#include "../core/interfaces/http_transport.h"

class Agent;

class WeChatGateway {
public:
    WeChatGateway(std::string webhook_url, Agent& agent,
                  std::shared_ptr<IHttpTransport> transport,
                  std::filesystem::path workspace_root);

    int run();   // Blocking: runs the polling loop
    void stop();

private:
    std::string webhook_url_;  // e.g. http://localhost:3001
    Agent& agent_;
    std::shared_ptr<IHttpTransport> transport_;
    std::filesystem::path workspace_root_;
    std::atomic<bool> running_{true};

    // Message handling
    void poll_messages();
    void handle_message(const std::string& from, const std::string& content);
    void send_message(const std::string& to, const std::string& text);
};

#endif
