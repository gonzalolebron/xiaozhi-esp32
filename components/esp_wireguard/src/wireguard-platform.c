#include "wireguard-platform.h"

#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <lwip/sys.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <sys/time.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_log.h>

#include "crypto.h"

#define TAG "wireguard-platform"

// Sofia: upstream seeded an mbedtls CTR-DRBG from the hardware generator. In
// ESP-IDF 6 (mbedtls 4) entropy.h and ctr_drbg.h are private headers, so this
// asks the hardware generator directly; with the radio on, as it always is
// when a tunnel is wanted, esp_fill_random() is a true random source.
// Sofia: the handshake carries a timestamp, and the other end refuses any that
// is not later than the last one it saw from this key. Upstream read the system
// clock each time. On this firmware the system clock is UTC after SNTP and then
// LOCAL time once the server's OTA reply arrives (ota.cc adds the timezone), so
// a board that had talked at local time was refused for two hours after its
// next boot (2026-10-12: handshakes reached the house and got no answer). The
// clock is read once, here, right after SNTP, and from then on the timestamp
// follows the monotonic timer.
static int64_t epoch_minus_uptime_us;

esp_err_t wireguard_platform_init() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	epoch_minus_uptime_us = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec - esp_timer_get_time();
	return ESP_OK;
}

void wireguard_random_bytes(void *bytes, size_t size) {
	esp_fill_random(bytes, size);
}

uint32_t wireguard_sys_now() {
	// Default to the LwIP system time
	return sys_now();
}

void wireguard_tai64n_now(uint8_t *output) {
	// See https://cr.yp.to/libtai/tai64.html
	// 64 bit seconds from 1970 = 8 bytes
	// 32 bit nano seconds from current second

	const int64_t now_us = epoch_minus_uptime_us + esp_timer_get_time();

	uint64_t seconds = 0x400000000000000aULL + (uint64_t)(now_us / 1000000LL);
	uint32_t nanos = (uint32_t)(now_us % 1000000LL) * 1000;
	U64TO8_BIG(output + 0, seconds);
	U32TO8_BIG(output + 8, nanos);
}

bool wireguard_is_under_load() {
	return false;
}
// vim: noexpandtab
