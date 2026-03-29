# ⚡ Bolt

A blazing-fast autonomous coding agent written in C++. Single binary, zero dependencies, 0.1ms framework overhead.

> Think of it as a local Claude Code / Cursor / Aider alternative that runs as a single native executable.

[English](#features) | [中文](README_CN.md)

## Why C++?

| | Python agents | Bolt |
|---|---|---|
| **Startup** | 2-5 seconds | **<50ms** |
| **Framework overhead** | 10-50ms/turn | **0.1ms/turn** |
| **Deployment** | Python + pip + venv | **Single binary** |
| **Memory** | 200-500MB | **<30MB** |
| **Parallel tools** | GIL-limited | **True multi-core** |
| **Streaming latency** | Buffered | **Zero-copy SSE** |

## Features

- **19 built-in tools** — file ops, code search, code intelligence, build & test, task planning, git, shell commands, desktop automation
- **5 LLM providers** — Ollama (local), OpenAI, Claude, Gemini, Groq + any OpenAI-compatible API
- **Autonomous loop** — edit → compile → test → fix → repeat until passing
- **Code intelligence** — find definitions, references, classes across C++/Python/JS/Rust/Go
- **MCP protocol** — `bolt mcp-server` exposes all tools to Claude Code, Cursor, etc.
- **Plugin system** — extend with tools written in any language (Python, Node, Go, etc.)
- **Session persistence** — save/restore conversations across sessions
- **VS Code extension** — sidebar chat, explain/fix selection, generate tests
- **Web UI** — dark theme, Markdown rendering, code highlighting, SSE streaming
- **Performance engine** — thread pool, trigram index, speculative prefetch, HTTP/2
- **Cross-platform** — Windows, Linux, macOS
- **Docker** — `docker run` one-command deployment

## Quick Start

### Install via npm

```bash
npm install -g bolt-agent
bolt agent "Hello, what tools do you have?"
```

### Build from source

```bash
git clone https://github.com/General-zzz-trade/Bolt.git
cd Bolt
cmake -B build -S .
cmake --build build -j8
./build/bolt agent "Read src/main.cpp and explain it"
```

**Requirements**: C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+), CMake 3.10+

### With Ollama (local AI, no API key needed)

```bash
# Install Ollama: https://ollama.ai
ollama pull qwen3:8b
bolt agent "Read CMakeLists.txt and list all build targets"
```

### Docker

```bash
docker build -t bolt .
docker run -v $(pwd):/workspace bolt agent "List the files"
```

## Usage

### CLI

```bash
# Interactive mode (default)
bolt

# Single-turn
bolt agent "Search the codebase for TODO comments"

# Web UI
bolt web-chat --port 8080

# MCP server (for Claude Code / Cursor integration)
bolt mcp-server

# Performance benchmark
bolt bench --rounds 5

# Help
bolt --help
```

### Interactive Commands

```
⚡ Bolt — AI Coding Agent

❯ Read src/main.cpp and explain it        # Ask anything
❯ /save                                    # Save current session
❯ /load session-xxx                        # Restore a session
❯ /sessions                                # List saved sessions
❯ /clear                                   # Clear history
❯ /help                                    # Show help
❯ /quit                                    # Exit
```

### Configure LLM Provider

Create `bolt.conf` in your project root:

```ini
provider = ollama-chat          # ollama-chat | openai | claude | gemini | groq

# Auto-approve tool calls for autonomous mode
approval.mode = auto-approve
```

Environment variables:
```bash
export OPENAI_API_KEY=sk-...
export ANTHROPIC_API_KEY=sk-ant-...
export GEMINI_API_KEY=AI...
export GROQ_API_KEY=gsk_...
```

### VS Code Extension

```bash
cd vscode-extension
npm install && npm run compile
# F5 in VS Code to launch, or:
npx vsce package  # creates .vsix
```

Features: sidebar chat, right-click "Explain Selection" / "Fix Selection" / "Generate Tests", configurable provider/model.

### MCP Protocol (Claude Code / Cursor Integration)

Add to your MCP client config:

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

All 19 tools are automatically exposed via JSON-RPC 2.0.

### Plugin System

Write a plugin in any language:

```python
#!/usr/bin/env python3
# my_plugin.py — Bolt reads JSON from stdin, writes JSON to stdout
import json, sys
req = json.loads(input())
if req["method"] == "describe":
    print(json.dumps({"name": "my_tool", "description": "My custom tool"}))
elif req["method"] == "run":
    result = f"Got args: {req['params']['args']}"
    print(json.dumps({"success": True, "result": result}))
```

## Available Tools

| Tool | Description | Auto-approved |
|------|-------------|:---:|
| `read_file` | Read file contents (up to 32KB) | Yes |
| `list_dir` | List directory contents | Yes |
| `search_code` | Full-text search across workspace | Yes |
| `code_intel` | Find definitions, references, classes, includes | Yes |
| `calculator` | Arithmetic expressions | Yes |
| `task_planner` | Create and track multi-step plans | Yes |
| `edit_file` | Modify existing files (exact text replacement) | Needs approval |
| `write_file` | Create new files | Needs approval |
| `build_and_test` | Auto-detect build system, compile, run tests | Needs approval |
| `run_command` | Shell commands (35+ whitelisted tools, full git) | Needs approval |
| `list_processes` | List running processes | Yes |
| `list_windows` | List visible windows (Windows) | Yes |
| `open_app` | Launch applications | Needs approval |
| `focus_window` | Bring window to foreground | Needs approval |
| `wait_for_window` | Wait for window to appear | Yes |
| `inspect_ui` | Inspect UI element tree (Windows) | Yes |
| `click_element` | Click UI elements | Needs approval |
| `type_text` | Type text into focused window | Needs approval |

## Architecture

```
                    +-----------------+
                    |   LLM Provider  |
                    | (Ollama/OpenAI/ |
                    | Claude/Gemini)  |
                    +--------+--------+
                             |
                    +--------v--------+
                    |   Agent Loop    |     ← 0.1ms overhead
                    | (50 steps max)  |
                    +--------+--------+
                             |
         +-------------------+-------------------+
         |                   |                   |
   +-----v------+    +------v------+    +-------v------+
   | Tool Engine |    | Thread Pool |    |  File Index  |
   | (19+ tools) |    | (parallel)  |    |  (trigram)   |
   +-----+------+    +------+------+    +-------+------+
         |                   |                   |
   +-----v------------------v-------------------v------+
   |                 Workspace (your code)              |
   +----------------------------------------------------+
         |                   |                   |
   +-----v------+    +------v------+    +-------v------+
   | MCP Server  |    |  Plugin Sys |    | Session Store|
   | (JSON-RPC)  |    | (subprocess)|    |   (.json)    |
   +-------------+    +-------------+    +--------------+
```

## Benchmark

```
=== Framework Overhead (no network) ===
  Agent loop:               0.1ms
  JSON serialize (10 tools): 0.2ms
  Tool lookup (1000x):      0.1ms  (O(1) hash)
  8 tools parallel (pool):  1.2ms  vs sequential 4.0ms  (3.4x speedup)
  File index build:         173ms  (178 files, 1.5MB)
  Prefetch cache:           593x faster than disk

=== Ollama qwen3:8b (local) ===
  Cold connect:             236ms
  TTFT:                     164ms
  Token throughput:         14.9 tok/s
  Streaming overhead:       +0.8%
```

Run your own benchmark:
```bash
bolt bench --provider ollama-chat --rounds 5
bolt bench --json > results.json
```

## Project Structure

```
src/
  agent/            # Agent loop, 19 tools, task runner, plugin system
  app/              # CLI, web server, config, benchmark, session
  core/
    caching/        # Tool result cache
    config/         # Runtime, policy, command configs
    indexing/       # Trigram file index, speculative prefetch
    interfaces/     # Abstract interfaces (IModelClient, IFileSystem, ...)
    mcp/            # MCP protocol server (JSON-RPC 2.0)
    model/          # ChatMessage, ToolSchema
    net/            # SSE parser
    routing/        # Model router, prompt compressor
    session/        # Session persistence
    threading/      # Thread pool
  platform/
    linux/          # POSIX sockets, fork/exec, /proc
    windows/        # WinHTTP, UI Automation, CreateProcess
  providers/        # OpenAI, Claude, Gemini, Ollama, Groq clients
web/                # Browser UI (dark theme, Markdown, code highlight)
vscode-extension/   # VS Code sidebar chat + commands
npm/                # npm package wrapper
tests/              # 120 tests (kernel + integration + capability)
third_party/        # nlohmann/json
```

## Contributing

Contributions welcome! See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

```bash
cmake -B build -S .
cmake --build build -j8
./build/kernel_tests            # 76 unit tests
./build/agent_integration_tests # 3 integration tests
./build/capability_tests        # 41 capability tests
```

Areas we need help with:
- **Tree-sitter AST** — replace regex code intelligence with proper parsing
- **More providers** — Mistral, Cohere, local GGUF inference
- **Multi-agent** — parallel task execution across multiple agents
- **RAG / vector search** — semantic code retrieval for large codebases

## Roadmap

- [x] Multi-provider LLM support (Ollama, OpenAI, Claude, Gemini, Groq)
- [x] Autonomous edit → build → test → fix loop
- [x] Code intelligence (definitions, references, classes)
- [x] Task planning and progress tracking
- [x] Performance benchmark suite
- [x] Thread pool parallel tool execution
- [x] Trigram file index + speculative prefetch
- [x] SSE streaming Web UI with Markdown + code highlighting
- [x] Cross-platform (Windows, Linux, macOS)
- [x] GitHub Actions CI/CD
- [x] MCP protocol server
- [x] Plugin system (JSON-RPC subprocess)
- [x] Session persistence
- [x] VS Code extension
- [x] Docker image
- [x] npm package (bolt-agent)
- [x] HTTPS support (curl-based, zero dependencies)
- [ ] Pre-built release binaries
- [ ] Tree-sitter AST integration
- [ ] Multi-agent collaboration
- [ ] RAG / vector search
- [ ] VS Code marketplace publish

## License

MIT
