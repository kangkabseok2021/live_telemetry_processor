#include "UdpReceiver.h"
#include "PacketParser.h"
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <iostream>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <unistd.h>

static void try_realtime_scheduling() noexcept {
    sched_param sp{};
    sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0)
        std::cerr << "[UdpReceiver] SCHED_FIFO unavailable — using default\n";
}

UdpReceiver::UdpReceiver(Queue& queue, uint16_t port)
    : queue_(queue), port_(port) {}

UdpReceiver::~UdpReceiver() { stop(); }

void UdpReceiver::start() {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) { perror("socket"); return; }

    // Tune receive buffer to absorb bursts at 1000+ Hz
    int buf = 4 * 1024 * 1024;
    setsockopt(sock_, SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind"); close(sock_); sock_ = -1; return;
    }

    thread_ = std::jthread([this](std::stop_token st){ run(st); });
}

void UdpReceiver::stop() {
    thread_.request_stop();
    if (sock_ >= 0) { shutdown(sock_, SHUT_RDWR); close(sock_); sock_ = -1; }
}

void UdpReceiver::run(std::stop_token st) {
    try_realtime_scheduling();

    alignas(alignof(TelemetryFrame)) std::byte buf[2048];
    TelemetryFrame frame{};

    while (!st.stop_requested()) {
        ssize_t n = recv(sock_, buf, sizeof(buf), 0);
        if (n <= 0) break;

        auto err = parse_frame({buf, static_cast<std::size_t>(n)}, &frame);
        if (err != ParseError::OK) continue;

        received_.fetch_add(1, std::memory_order_relaxed);
        if (!queue_.push(frame))
            dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}
