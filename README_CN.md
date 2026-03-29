# ⚡ Bolt

极速自主编码智能体，纯 C++ 实现。单文件部署，零依赖，0.1ms 框架开销。

> 可以理解为本地版的 Claude Code / Cursor / Aider，以单个原生可执行文件运行。

[English](README.md) | 中文

## 为什么用 C++？

| | Python 智能体 | Bolt |
|---|---|---|
| **启动时间** | 2-5 秒 | **<50ms** |
| **框架开销** | 10-50ms/轮 | **0.1ms/轮** |
| **部署方式** | Python + pip + venv | **单个二进制文件** |
| **内存占用** | 200-500MB | **<30MB** |
| **并行工具** | 受 GIL 限制 | **真正多核并行** |
| **流式延迟** | 缓冲输出 | **零拷贝 SSE** |

## 功能特性

- **19 个内置工具** — 文件操作、代码搜索、代码智能、编译测试、任务规划、Git、Shell 命令、桌面自动化
- **5 个 LLM 提供商** — Ollama（本地）、OpenAI、Claude、Gemini、Groq + 任何 OpenAI 兼容 API
- **自主循环** — 编辑 → 编译 → 测试 → 修复 → 重复直到通过
- **代码智能** — 跨 C++/Python/JS/Rust/Go 查找定义、引用、类
- **MCP 协议** — `bolt mcp-server` 将所有工具暴露给 Claude Code、Cursor 等
- **插件系统** — 用任何语言（Python、Node、Go 等）编写扩展工具
- **会话持久化** — 跨会话保存/恢复对话
- **VS Code 扩展** — 侧边栏聊天、解释/修复选中代码、生成测试
- **Web UI** — 暗色主题、Markdown 渲染、代码高亮、SSE 流式输出
- **性能引擎** — 线程池、三元组索引、投机预取、HTTP/2 多路复用
- **跨平台** — Windows、Linux、macOS
- **Docker** — `docker run` 一行命令部署

## 快速开始

### npm 安装

```bash
npm install -g bolt-agent
bolt agent "你好，你有什么工具？"
```

### 从源码编译

```bash
git clone https://github.com/General-zzz-trade/Bolt.git
cd Bolt
cmake -B build -S .
cmake --build build -j8
./build/bolt agent "读取 src/main.cpp 并解释它"
```

**环境要求**：C++17 编译器（GCC 9+、Clang 10+、MSVC 2019+）、CMake 3.10+

### 搭配 Ollama（本地 AI，无需 API Key）

```bash
# 安装 Ollama：https://ollama.ai
ollama pull qwen3:8b
bolt agent "读取 CMakeLists.txt 并列出所有构建目标"
```

### Docker

```bash
docker build -t bolt .
docker run -v $(pwd):/workspace bolt agent "列出文件"
```

## 使用方式

### 命令行

```bash
# 交互模式（默认）
bolt

# 单轮对话
bolt agent "搜索代码库中的 TODO 注释"

# Web 界面
bolt web-chat --port 8080

# MCP 服务器（集成 Claude Code / Cursor）
bolt mcp-server

# 性能基准测试
bolt bench --rounds 5

# 帮助
bolt --help
```

### 交互命令

```
⚡ Bolt — AI Coding Agent

❯ 读取 src/main.cpp 并解释它             # 提问任何问题
❯ /save                                    # 保存当前会话
❯ /load session-xxx                        # 恢复会话
❯ /sessions                                # 列出已保存的会话
❯ /clear                                   # 清空历史
❯ /help                                    # 显示帮助
❯ /quit                                    # 退出
```

### 配置 LLM 提供商

在项目根目录创建 `bolt.conf`：

```ini
provider = ollama-chat          # ollama-chat | openai | claude | gemini | groq

# 自动批准所有工具调用（自主模式）
approval.mode = auto-approve
```

环境变量：
```bash
export OPENAI_API_KEY=sk-...
export ANTHROPIC_API_KEY=sk-ant-...
export GEMINI_API_KEY=AI...
export GROQ_API_KEY=gsk_...
```

### VS Code 扩展

```bash
cd vscode-extension
npm install && npm run compile
# 在 VS Code 中按 F5 启动，或：
npx vsce package  # 生成 .vsix 文件
```

功能：侧边栏聊天、右键"解释选中代码"/"修复选中代码"/"生成测试"、可配置提供商/模型。

### MCP 协议（Claude Code / Cursor 集成）

添加到 MCP 客户端配置：

```json
{
  "mcpServers": {
    "bolt": {
      "command": "bolt",
      "args": ["mcp-server"]
    }
  }
}
```

所有 19 个工具自动通过 JSON-RPC 2.0 暴露。

### 插件系统

用任何语言编写插件：

```python
#!/usr/bin/env python3
# my_plugin.py — Bolt 从 stdin 读取 JSON，向 stdout 写入 JSON
import json, sys
req = json.loads(input())
if req["method"] == "describe":
    print(json.dumps({"name": "my_tool", "description": "我的自定义工具"}))
elif req["method"] == "run":
    result = f"收到参数: {req['params']['args']}"
    print(json.dumps({"success": True, "result": result}))
```

## 内置工具

| 工具 | 功能 | 自动批准 |
|------|------|:---:|
| `read_file` | 读取文件内容（最大 32KB） | 是 |
| `list_dir` | 列出目录内容 | 是 |
| `search_code` | 全文搜索工作区 | 是 |
| `code_intel` | 查找定义、引用、类、包含关系 | 是 |
| `calculator` | 算术表达式计算 | 是 |
| `task_planner` | 创建和跟踪多步任务计划 | 是 |
| `edit_file` | 修改现有文件（精确文本替换） | 需批准 |
| `write_file` | 创建新文件 | 需批准 |
| `build_and_test` | 自动检测构建系统、编译、运行测试 | 需批准 |
| `run_command` | Shell 命令（35+ 白名单工具，完整 Git） | 需批准 |
| `list_processes` | 列出运行中的进程 | 是 |
| `list_windows` | 列出可见窗口（Windows） | 是 |
| `open_app` | 启动应用程序 | 需批准 |
| `focus_window` | 将窗口置前 | 需批准 |
| `wait_for_window` | 等待窗口出现 | 是 |
| `inspect_ui` | 检查 UI 元素树（Windows） | 是 |
| `click_element` | 点击 UI 元素 | 需批准 |
| `type_text` | 向聚焦窗口输入文本 | 需批准 |

## 架构

```
                    +-----------------+
                    |   LLM 提供商    |
                    | (Ollama/OpenAI/ |
                    | Claude/Gemini)  |
                    +--------+--------+
                             |
                    +--------v--------+
                    |    Agent 循环   |     ← 0.1ms 开销
                    |  (最多 50 步)   |
                    +--------+--------+
                             |
         +-------------------+-------------------+
         |                   |                   |
   +-----v------+    +------v------+    +-------v------+
   |  工具引擎   |    |   线程池    |    |   文件索引   |
   | (19+ 工具) |    |  (并行执行)  |    |  (三元组)    |
   +-----+------+    +------+------+    +-------+------+
         |                   |                   |
   +-----v------------------v-------------------v------+
   |                工作区（你的代码）                    |
   +----------------------------------------------------+
         |                   |                   |
   +-----v------+    +------v------+    +-------v------+
   | MCP 服务器  |    |  插件系统   |    |  会话存储    |
   | (JSON-RPC) |    | (子进程)    |    |  (.json)     |
   +-------------+    +-------------+    +--------------+
```

## 性能基准

```
=== 框架开销（无网络） ===
  Agent 循环:            0.1ms
  JSON 序列化 (10 工具): 0.2ms
  工具查找 (1000次):     0.1ms  (O(1) 哈希)
  8 工具并行 (线程池):   1.2ms  对比串行 4.0ms  (3.4x 加速)
  文件索引构建:          173ms  (178 文件, 1.5MB)
  预取缓存:              593x 快于磁盘读取

=== Ollama qwen3:8b (本地) ===
  冷启动连接:            236ms
  首字延迟:              164ms
  生成吞吐:              14.9 tok/s
  流式开销:              +0.8%
```

运行你自己的基准测试：
```bash
bolt bench --provider ollama-chat --rounds 5
bolt bench --json > results.json
```

## 项目结构

```
src/
  agent/            # Agent 循环、19 个工具、任务执行器、插件系统
  app/              # CLI、Web 服务器、配置、基准测试、会话
  core/
    caching/        # 工具结果缓存
    config/         # 运行时、策略、命令配置
    indexing/       # 三元组文件索引、投机预取
    interfaces/     # 抽象接口 (IModelClient, IFileSystem, ...)
    mcp/            # MCP 协议服务器 (JSON-RPC 2.0)
    model/          # ChatMessage, ToolSchema
    net/            # SSE 解析器
    routing/        # 模型路由器、提示压缩器
    session/        # 会话持久化
    threading/      # 线程池
  platform/
    linux/          # POSIX socket、fork/exec、/proc
    windows/        # WinHTTP、UI 自动化、CreateProcess
  providers/        # OpenAI、Claude、Gemini、Ollama、Groq 客户端
web/                # 浏览器 UI（暗色主题、Markdown、代码高亮）
vscode-extension/   # VS Code 侧边栏聊天 + 命令
npm/                # npm 包装器
tests/              # 120 个测试（内核 + 集成 + 能力验证）
third_party/        # nlohmann/json
```

## 参与贡献

欢迎贡献！详见 [CONTRIBUTING.md](CONTRIBUTING.md)。

```bash
cmake -B build -S .
cmake --build build -j8
./build/kernel_tests            # 76 个单元测试
./build/agent_integration_tests # 3 个集成测试
./build/capability_tests        # 41 个能力验证测试
```

我们需要帮助的方向：
- **Tree-sitter AST** — 用正式的 AST 解析替代 regex 代码智能
- **更多提供商** — Mistral、Cohere、本地 GGUF 推理
- **多 Agent 协作** — 跨多个 agent 并行执行任务
- **RAG / 向量搜索** — 大型代码库的语义检索

## 路线图

- [x] 多提供商 LLM 支持 (Ollama, OpenAI, Claude, Gemini, Groq)
- [x] 自主 编辑 → 编译 → 测试 → 修复 循环
- [x] 代码智能（定义、引用、类）
- [x] 任务规划和进度跟踪
- [x] 性能基准测试套件
- [x] 线程池并行工具执行
- [x] 三元组文件索引 + 投机预取
- [x] SSE 流式 Web UI（Markdown + 代码高亮）
- [x] 跨平台 (Windows, Linux, macOS)
- [x] GitHub Actions CI/CD
- [x] MCP 协议服务器
- [x] 插件系统（JSON-RPC 子进程）
- [x] 会话持久化
- [x] VS Code 扩展
- [x] Docker 镜像
- [x] npm 包 (bolt-agent)
- [x] HTTPS 支持（基于 curl，零依赖）
- [ ] 预编译发布二进制
- [ ] Tree-sitter AST 集成
- [ ] 多 Agent 协作
- [ ] RAG / 向量搜索
- [ ] VS Code 市场发布

## 许可证

MIT
