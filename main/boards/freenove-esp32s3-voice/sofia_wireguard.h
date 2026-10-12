// SPIKE (spec 027): a WireGuard tunnel from the board to the Mac mini, so she
// works on a network that is not the house's — a phone's hotspot — with nothing
// else to carry.
//
// At home the server answers directly and no tunnel is started: the house must
// not depend on it. Away, the clock is set from the internet (the handshake
// carries a timestamp the other end refuses if it goes backwards), the tunnel
// comes up against the house's public name, and it becomes the default route:
// from then on 192.168.0.105 exists again, and the firmware's own retries find
// the server without knowing anything changed.
//
// The keys come from sofia_wireguard_secrets.h, written on the build machine
// from files outside every repository and ignored by git.
#pragma once

#include <cstring>
#include <ctime>
#include <sys/time.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wireguard.h>
#include <esp_netif_sntp.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include "wifi_manager.h"
#include "sofia_wireguard_secrets.h"

extern "C" uint32_t sofia_wg_nat_home;     // components/esp_wireguard/src/wireguardif.c
extern "C" uint32_t sofia_wg_nat_peer;

namespace sofia_wireguard {

static const char* const WG_TAG = "SofiaWireguard";

// Milliseconds to open a TCP connection and get the first bytes of the OTA
// page, or -1. Never waits more than timeout_s for the connection: an address
// with no route can otherwise hold connect() for many seconds.
inline int ProbeMs(const char* ip, int port, int timeout_s = 4) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    inet_pton(AF_INET, ip, &to.sin_addr);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    const int64_t t0 = esp_timer_get_time();
    int ms = -1;
    connect(fd, reinterpret_cast<struct sockaddr*>(&to), sizeof(to));
    fd_set ws;
    FD_ZERO(&ws);
    FD_SET(fd, &ws);
    struct timeval tv = {timeout_s, 0};
    int so_error = -1;
    socklen_t len = sizeof(so_error);
    if (select(fd + 1, nullptr, &ws, nullptr, &tv) == 1 &&
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) == 0 && so_error == 0) {
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        static const char req[] = "GET /xiaozhi/ota/ HTTP/1.0\r\n\r\n";
        char buf[16];
        if (send(fd, req, sizeof(req) - 1, 0) > 0 && recv(fd, buf, sizeof(buf), 0) > 0) {
            ms = static_cast<int>((esp_timer_get_time() - t0) / 1000);
        }
    }
    close(fd);
    return ms;
}

inline void Report(const char* what) {
    ESP_LOGI(WG_TAG, "%s: internal heap free %u, minimum %u", what,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
}

inline void Task(void*) {
    static char private_key[] = SOFIA_WG_PRIVATE_KEY;
    static char peer_key[] = SOFIA_WG_PEER_PUBLIC_KEY;
    static char address[] = SOFIA_WG_ADDRESS;
    static char mask[] = "255.255.255.0";
    static char endpoint[] = SOFIA_WG_ENDPOINT;
    static wireguard_config_t config = ESP_WIREGUARD_CONFIG_DEFAULT();
    static wireguard_ctx_t ctx = {};

    while (!WifiManager::GetInstance().IsInitialized() || !WifiManager::GetInstance().IsConnected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    const int direct = ProbeMs(SOFIA_WG_HOME_SERVER, 8003, 3);
    if (direct >= 0) {
        ESP_LOGI(WG_TAG, "at home: the server answers directly (%d ms), no tunnel", direct);
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(WG_TAG, "away: the server does not answer directly, starting the tunnel");

    // Always from the internet, never whatever the clock holds: the tunnel keeps
    // its own UTC from here on (see wireguard_platform_init).
    {
        esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_netif_sntp_init(&sntp);
        const esp_err_t synced = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(20000));
        ESP_LOGI(WG_TAG, "clock from the internet: %s", synced == ESP_OK ? "set" : "NOT set, the handshake may be refused");
    }

    Report("before the tunnel");
    config.private_key = private_key;
    config.public_key = peer_key;
    config.allowed_ip = address;
    config.allowed_ip_mask = mask;
    config.endpoint = endpoint;
    config.port = SOFIA_WG_PORT;
    config.persistent_keepalive = 25;
    esp_err_t err = esp_wireguard_init(&config, &ctx);
    if (err == ESP_OK) err = esp_wireguard_connect(&ctx);
    if (err != ESP_OK) {
        ESP_LOGE(WG_TAG, "could not start the tunnel: %s", esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }
    const int64_t started = esp_timer_get_time();

    bool up = false;
    for (int tick = 0;; ++tick) {
        vTaskDelay(pdMS_TO_TICKS(up ? 10000 : 1000));
        const bool now_up = esp_wireguardif_peer_is_up(&ctx) == ESP_OK;
        if (now_up && !up) {
            // Inside the tunnel the server answers at its tunnel address, not at
            // the house one the firmware knows it by: see sofia_nat().
            sofia_wg_nat_peer = inet_addr(SOFIA_WG_PEER_ADDRESS);
            sofia_wg_nat_home = inet_addr(SOFIA_WG_HOME_SERVER);
            esp_wireguard_set_default(&ctx);
            ESP_LOGI(WG_TAG, "tunnel UP after %d ms: everything goes home through it now",
                     static_cast<int>((esp_timer_get_time() - started) / 1000));
        }
        if (now_up != up) { up = now_up; Report(up ? "tunnel up" : "tunnel DOWN"); }
        if (up && tick % 3 == 0) {
            const int ms = ProbeMs(SOFIA_WG_HOME_SERVER, 8003);
            ESP_LOGI(WG_TAG, "the server through the tunnel: %d ms (internal heap free %u, minimum %u)", ms,
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                     static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
        }
    }
}

inline void Start() { xTaskCreate(Task, "sofia_wg", 6144, nullptr, 2, nullptr); }

}  // namespace sofia_wireguard
