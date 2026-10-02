#pragma once
// The board's log, sent to the server over UDP (spec 018).
//
// Every line that goes out of the serial port is also sent to the voice server's
// log daemon, so that when she does not wake and nobody had a cable plugged in
// there is still a trace. It never makes anything wait:
//   - the hook only copies the line into a small queue, with no waiting; when the
//     queue is full the OLDEST line is dropped (the last ones are the ones that
//     explain a failure);
//   - a task of its own, at the lowest priority, sends them in batches;
//   - nothing in the send path logs, or it would feed itself.
// The destination is the host of the MQTT endpoint the OTA handed the board: the
// server's address, with no configuration of its own.
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "settings.h"

namespace sofia_netlog {

constexpr int kPort = 8005;
constexpr int kLineBytes = 200;
constexpr int kQueueLines = 48;
constexpr int kBatchBytes = 1000;

static vprintf_like_t g_previous = nullptr;
static QueueHandle_t g_queue = nullptr;

struct Line {
    char text[kLineBytes];
};

static int Hook(const char* fmt, va_list args) {
    va_list copy;
    va_copy(copy, args);
    int written = g_previous ? g_previous(fmt, args) : 0;
    if (g_queue != nullptr) {
        Line line;
        vsnprintf(line.text, sizeof(line.text), fmt, copy);
        if (xQueueSend(g_queue, &line, 0) != pdTRUE) {
            Line dropped;
            xQueueReceive(g_queue, &dropped, 0);     // make room: lose the oldest
            xQueueSend(g_queue, &line, 0);
        }
    }
    va_end(copy);
    return written;
}

// The server's address, once the OTA has handed it over. Empty until then.
static std::string ServerHost() {
    Settings settings("mqtt", false);
    std::string endpoint = settings.GetString("endpoint");
    size_t colon = endpoint.find(':');
    return colon == std::string::npos ? endpoint : endpoint.substr(0, colon);
}

// The TCP/IP stack does not exist for the first seconds of the boot; opening a
// socket then is an assert and a reboot loop (found the hard way, 2026-10-02).
// The WiFi station having an address means it is up.
static bool NetworkIsUp() {
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta == nullptr) return false;
    esp_netif_ip_info_t ip;
    return esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr != 0;
}

static void SenderTask(void*) {
    int sock = -1;
    sockaddr_in dest = {};
    std::string host;
    TickType_t checked = 0;
    char batch[kBatchBytes + kLineBytes];
    for (;;) {
        Line first;
        if (xQueueReceive(g_queue, &first, portMAX_DELAY) != pdTRUE) continue;
        size_t used = strlcpy(batch, first.text, sizeof(batch));
        Line next;
        // Whatever else is already waiting goes in the same datagram.
        while (used < kBatchBytes && xQueueReceive(g_queue, &next, pdMS_TO_TICKS(30)) == pdTRUE) {
            used += strlcpy(batch + used, next.text, sizeof(batch) - used);
        }

        if (!NetworkIsUp()) continue;                   // nothing to send over yet: drop
        // Opening the settings costs: look again only every 10 s, or until it is known.
        TickType_t tick = xTaskGetTickCount();
        std::string now_host = host;
        if (host.empty() || tick - checked > pdMS_TO_TICKS(10000)) {
            now_host = ServerHost();
            checked = tick;
        }
        if (now_host.empty()) continue;                 // not told yet: drop
        if (sock < 0 || now_host != host) {
            if (sock >= 0) close(sock);
            host = now_host;
            sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
            dest = {};
            dest.sin_family = AF_INET;
            dest.sin_port = htons(kPort);
            if (sock < 0 || inet_pton(AF_INET, host.c_str(), &dest.sin_addr) != 1) {
                if (sock >= 0) close(sock);
                sock = -1;
                continue;
            }
        }
        sendto(sock, batch, used, 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    }
}

inline void Start() {
    if (g_queue != nullptr) return;
    g_queue = xQueueCreate(kQueueLines, sizeof(Line));
    if (g_queue == nullptr) return;
    xTaskCreate(SenderTask, "netlog", 4096, nullptr, 1, nullptr);
    g_previous = esp_log_set_vprintf(Hook);
    ESP_LOGI("netlog", "boot: reset reason %d, log goes to the server on udp/%d",
             static_cast<int>(esp_reset_reason()), kPort);
}

}  // namespace sofia_netlog
