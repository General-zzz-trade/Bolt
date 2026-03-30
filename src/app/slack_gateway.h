#ifndef APP_SLACK_GATEWAY_H
#define APP_SLACK_GATEWAY_H

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

#include "../core/interfaces/http_transport.h"

class Agent;

class SlackGateway {
public:
    SlackGateway(std::string bot_token, std::string channel_id,
                 Agent& agent, std::shared_ptr<IHttpTransport> transport,
                 std::filesystem::path workspace_root);

    int run();   // Blocking: runs the polling loop
    void stop();

private:
    std::string bot_token_;
    std::string channel_id_;
    Agent& agent_;
    std::shared_ptr<IHttpTransport> transport_;
    std::filesystem::path workspace_root_;
    std::atomic<bool> running_{true};
    std::string last_ts_;
    std::string bot_user_id_;

    // Slack API helpers
    std::string api_call(const std::string& method, const std::string& path,
                         const std::string& body = "");

    // Message handling
    void poll_messages();
    void handle_message(const std::string& channel_id, const std::string& content);
    void send_message(const std::string& channel_id, const std::string& text);
};

#endif
