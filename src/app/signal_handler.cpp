#include "signal_handler.h"

SignalHandler& SignalHandler::instance() {
    static SignalHandler handler;
    return handler;
}

void SignalHandler::install() {
    update_dimensions();

    struct sigaction sa_int{};
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, nullptr);

    struct sigaction sa_winch{};
    sa_winch.sa_handler = sigwinch_handler;
    sigemptyset(&sa_winch.sa_mask);
    sa_winch.sa_flags = 0;
    sigaction(SIGWINCH, &sa_winch, nullptr);
}

bool SignalHandler::is_cancelled() const {
    return cancelled_.load(std::memory_order_acquire);
}

void SignalHandler::set_cancelled(bool value) {
    cancelled_.store(value, std::memory_order_release);
}

void SignalHandler::reset() {
    cancelled_.store(false, std::memory_order_release);
}

bool SignalHandler::resize_pending() const {
    return resize_pending_.load(std::memory_order_acquire);
}

void SignalHandler::clear_resize_pending() {
    resize_pending_.store(false, std::memory_order_release);
}

int SignalHandler::terminal_width() const {
    return terminal_width_.load(std::memory_order_relaxed);
}

int SignalHandler::terminal_height() const {
    return terminal_height_.load(std::memory_order_relaxed);
}

void SignalHandler::update_dimensions() {
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_col > 0) {
            terminal_width_.store(ws.ws_col, std::memory_order_relaxed);
        }
        if (ws.ws_row > 0) {
            terminal_height_.store(ws.ws_row, std::memory_order_relaxed);
        }
    }
}

void SignalHandler::sigint_handler(int /*sig*/) {
    instance().cancelled_.store(true, std::memory_order_release);
}

void SignalHandler::sigwinch_handler(int /*sig*/) {
    instance().resize_pending_.store(true, std::memory_order_release);
}
