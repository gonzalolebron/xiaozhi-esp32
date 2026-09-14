#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// Freenove ESP32-S3 (N16R8) with an INMP441 microphone and a MAX98357A amp.
// Sofia's board. The microphone pins are kept from the previous ESPHome node
// (~/esphome/esp32-freenove.yaml) so nothing has to be rewired.

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// Simplex I2S: microphone and speaker on separate buses. The INMP441 is a
// slave and does not share the bus well with the MAX98357A at a different
// sample rate (16 kHz in against 24 kHz out).
#define AUDIO_I2S_METHOD_SIMPLEX

// --- INMP441 microphone — ALREADY WIRED, do not touch ---
// Same pins the ESPHome YAML used.
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_13  // LRCLK
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_12  // BCLK
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_11  // SD

// --- MAX98357A amplifier ---
// Pins picked from the module's free ones. GPIO 26-37 avoided: on the N16R8
// they are taken by the flash and the octal PSRAM. Moved to 17/18/16 for the
// second board's PCB: the amplifier connects there now.
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_17
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_18
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_16

// The board's blue LED; in the previous YAML it meant a voice session was on.
#define BUILTIN_LED_GPIO        GPIO_NUM_2
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define TOUCH_BUTTON_GPIO       GPIO_NUM_NC
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC


// --- 240x240 round GC9A01 display over SPI ---
// Pins picked from what was left: audio takes 7, 11, 12, 13, 15 and 16, and
// the LED takes 2. On the N16R8, GPIO 26-32 (flash), 33-37 (octal PSRAM),
// 19/20 (USB) and 43/44 (serial console) are off limits as well.
//
// The upstream reference board (bread-compact-wifi-lcd) uses GPIO 45 for RES.
// Here it is 39: 45 is a strapping pin on the ESP32-S3 — it selects the flash
// voltage — and a pull-up resistor on the module can keep the board from
// booting.
// The five sit next to each other down the Freenove's right-hand column, in
// the same order as the module's signals: a straight run, no crossings. None
// of them is a strapping pin. 39-42 are also the pinned JTAG, but the S3 uses
// JTAG over USB, so they are free.
#define DISPLAY_CLK_PIN       GPIO_NUM_42  // SCL
#define DISPLAY_MOSI_PIN      GPIO_NUM_41  // SDA
#define DISPLAY_DC_PIN        GPIO_NUM_40  // DC
#define DISPLAY_CS_PIN        GPIO_NUM_39  // CS
#define DISPLAY_RST_PIN       GPIO_NUM_38  // RST
// The 1.28" module in use (GC9A01 VER1.0) has 7 pins — VCC, GND, SCL, SDA,
// DC, CS, RST — and no BLK: the backlight is hardwired inside, fed by the
// module's own regulator. There is no pin to control.
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_NC

#define LCD_TYPE_GC9A01_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR true
#define DISPLAY_RGB_ORDER LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0

#endif // _BOARD_CONFIG_H_
