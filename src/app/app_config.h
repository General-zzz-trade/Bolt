#ifndef APP_APP_CONFIG_H
#define APP_APP_CONFIG_H

#include <filesystem>
#include <string>

#include "../core/config/agent_runtime_config.h"
#include "../core/config/approval_config.h"
#include "../core/config/command_policy_config.h"
#include "../core/config/ollama_connection_config.h"
#include "../core/config/policy_config.h"
#include "../core/config/sandbox_config.h"

struct AppConfig {
    std::string default_model = "qwen3:8b";
    std::string provider = "ollama";  // ollama | ollama-chat | openai | claude | gemini | groq | deepseek | qwen | zhipu | moonshot | baichuan | doubao | router
    std::string openai_base_url = "https://api.openai.com";
    std::string openai_model = "gpt-4o";
    std::string claude_model = "claude-sonnet-4-20250514";
    std::string gemini_model = "gemini-2.0-flash";
    std::string groq_base_url = "https://api.groq.com/openai";
    std::string groq_model = "llama-3.3-70b-versatile";
    // Chinese LLM providers (all OpenAI-compatible)
    std::string deepseek_base_url = "https://api.deepseek.com";
    std::string deepseek_model = "deepseek-chat";
    std::string qwen_base_url = "https://dashscope.aliyuncs.com/compatible-mode/v1";
    std::string qwen_model = "qwen-plus";
    std::string zhipu_base_url = "https://open.bigmodel.cn/api/paas/v4";
    std::string zhipu_model = "glm-4-flash";
    std::string moonshot_base_url = "https://api.moonshot.cn/v1";
    std::string moonshot_model = "moonshot-v1-8k";
    std::string baichuan_base_url = "https://api.baichuan-ai.com/v1";
    std::string baichuan_model = "Baichuan4";
    std::string doubao_base_url = "https://ark.cn-beijing.volces.com/api/v3";
    std::string doubao_model = "doubao-pro-32k";
    // Router: fast model (groq) + strong model (claude/openai)
    std::string router_fast_provider = "groq";
    std::string router_strong_provider = "claude";
    OllamaConnectionConfig ollama;
    CommandPolicyConfig command_policy;
    PolicyConfig policy;
    AgentRuntimeConfig agent_runtime;
    ApprovalConfig approval;
    SandboxConfig sandbox;
};

AppConfig load_app_config(const std::filesystem::path& workspace_root);

#endif
