# esp_wireguard, as carried by Sofia's fork

This is [trombik/esp_wireguard](https://github.com/trombik/esp_wireguard) 0.9.0
(commit `9217c5b`, BSD-3-Clause, see `LICENSE`), copied here rather than pulled
from the component registry because it does not build or work on ESP-IDF 6.1 as
published. `components/` is in upstream's `.gitignore`: changes here need
`git add -f`.

What differs from 0.9.0, all marked `Sofia:` in the source:

| File | Change | Why |
|---|---|---|
| `src/wireguard-platform.c` | random bytes from `esp_fill_random()` | mbedtls 4 made `entropy.h` and `ctr_drbg.h` private |
| `src/wireguard-platform.c` | the handshake timestamp follows the monotonic timer from the moment the tunnel starts | the system clock is UTC after SNTP and local time after the OTA reply; the peer refuses a timestamp that goes backwards |
| `src/esp_wireguard.c` | the tunnel's own UDP is bound to the interface that was the default | otherwise, as the default route, it sent its handshakes into itself |
| `src/wireguardif.c` | `sofia_nat()`: one address is rewritten on the way out and back | the firmware knows the server by its LAN address; inside the tunnel it answers at its tunnel address |
| `CMakeLists.txt` | `-Wno-error`, `esp_timer` and `esp_hw_support` required | GCC 15 |

It also needs `CONFIG_ESP_NETIF_BRIDGE_EN=y`: without it esp-netif takes the
tunnel interface's state pointer for its own and the board crashes on boot.
