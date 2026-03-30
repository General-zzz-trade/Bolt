#include "email_tool.h"

#include <cstdlib>
#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

EmailTool::EmailTool(std::shared_ptr<IHttpTransport> transport)
    : transport_(std::move(transport)) {}

std::string EmailTool::name() const { return "send_email"; }

std::string EmailTool::description() const {
    return "Send an email. Requires SENDGRID_API_KEY env var. Args: to, subject, body.";
}

ToolSchema EmailTool::schema() const {
    ToolSchema s;
    s.name = "send_email";
    s.description = description();
    s.parameters.push_back({"to", "string", "Recipient email address", true});
    s.parameters.push_back({"subject", "string", "Email subject", true});
    s.parameters.push_back({"body", "string", "Email body text", true});
    return s;
}

ToolResult EmailTool::run(const std::string& args) const {
    try {
        auto j = json::parse(args);
        std::string to = j.value("to", "");
        std::string subject = j.value("subject", "");
        std::string body = j.value("body", "");

        if (to.empty() || subject.empty()) {
            return {false, "Missing 'to' or 'subject' field"};
        }

        const char* sg_key = std::getenv("SENDGRID_API_KEY");
        if (sg_key && std::string(sg_key).size() > 0) {
            // SendGrid API
            json email;
            email["personalizations"] = json::array({{{"to", json::array({{{"email", to}}})}}});
            email["from"] = {{"email", "bolt@agent.local"}};
            email["subject"] = subject;
            email["content"] = json::array({{{"type", "text/plain"}, {"value", body}}});

            // Override from address if set
            const char* from_env = std::getenv("BOLT_EMAIL_FROM");
            if (from_env) email["from"]["email"] = from_env;

            HttpRequest req;
            req.method = "POST";
            req.url = "https://api.sendgrid.com/v3/mail/send";
            req.headers.push_back({"Authorization", std::string("Bearer ") + sg_key});
            req.headers.push_back({"Content-Type", "application/json"});
            req.body = email.dump();
            req.timeout_ms = 15000;

            auto resp = transport_->send(req);
            if (resp.status_code >= 200 && resp.status_code < 300) {
                return {true, "Email sent to " + to + " (subject: " + subject + ")"};
            }
            return {false, "SendGrid error: HTTP " + std::to_string(resp.status_code) + " " + resp.body};
        }

        return {false, "No email service configured. Set SENDGRID_API_KEY environment variable."};
    } catch (const std::exception& e) {
        return {false, std::string("Email error: ") + e.what()};
    }
}
