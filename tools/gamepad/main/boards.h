#pragma once

#include <driver/gpio.h>

// Retro-Go key bitmask definitions (matching rg_key_t)
#define RG_KEY_UP     (1 << 0)
#define RG_KEY_RIGHT  (1 << 1)
#define RG_KEY_DOWN   (1 << 2)
#define RG_KEY_LEFT   (1 << 3)
#define RG_KEY_SELECT (1 << 4)
#define RG_KEY_START  (1 << 5)
#define RG_KEY_MENU   (1 << 6)
#define RG_KEY_OPTION (1 << 7)
#define RG_KEY_A      (1 << 8)
#define RG_KEY_B      (1 << 9)
#define RG_KEY_X      (1 << 10)
#define RG_KEY_Y      (1 << 11)
#define RG_KEY_L      (1 << 12)
#define RG_KEY_R      (1 << 13)

#if defined(CONFIG_IDF_TARGET_ESP32C3)
// =========================================================================
// Target: ESP32-C3 (SuperMini / Zero / DevKit)
// 12 dedicated external GPIOs (pins < 32 for single-cycle REG_READ)
// - Avoid GPIO 9 (BOOT pin) to prevent download mode latchup on power-on
// - Avoid GPIO 8 (Onboard LED) unless 12th button (L) is explicitly needed
// - MENU is mapped to virtual combo (SELECT + UP)
// =========================================================================
#define BOARD_TARGET_NAME "ESP32-C3 SuperMini"

#define PIN_UP      GPIO_NUM_0   // D-Pad UP
#define PIN_DOWN    GPIO_NUM_1   // D-Pad DOWN
#define PIN_LEFT    GPIO_NUM_2   // D-Pad LEFT
#define PIN_RIGHT   GPIO_NUM_3   // D-Pad RIGHT
#define PIN_A       GPIO_NUM_4   // Button A
#define PIN_B       GPIO_NUM_5   // Button B
#define PIN_X       GPIO_NUM_6   // Button X
#define PIN_Y       GPIO_NUM_7   // Button Y
#define PIN_L       GPIO_NUM_8   // Button L (Shared with onboard LED, open-drain protected)
#define PIN_R       GPIO_NUM_10  // Button R
#define PIN_SELECT  GPIO_NUM_20  // Button SELECT
#define PIN_START   GPIO_NUM_21  // Button START
#define PIN_MENU    GPIO_NUM_NC  // Free GPIO 9 (BOOT pin) from button duty; use SELECT + UP combo
#define PIN_LED     GPIO_NUM_8   // Onboard LED (Active LOW)

#elif defined(CONFIG_IDF_TARGET_ESP32S3)
// =========================================================================
// Target: ESP32-S3 (SuperMini / Zero / DevKit)
// =========================================================================
#define BOARD_TARGET_NAME "ESP32-S3 Gamepad"

#define PIN_UP      GPIO_NUM_1
#define PIN_DOWN    GPIO_NUM_2
#define PIN_LEFT    GPIO_NUM_3
#define PIN_RIGHT   GPIO_NUM_4
#define PIN_A       GPIO_NUM_5
#define PIN_B       GPIO_NUM_6
#define PIN_X       GPIO_NUM_7
#define PIN_Y       GPIO_NUM_8
#define PIN_L       GPIO_NUM_9
#define PIN_R       GPIO_NUM_10
#define PIN_SELECT  GPIO_NUM_11
#define PIN_START   GPIO_NUM_12
#define PIN_MENU    GPIO_NUM_13
#define PIN_LED     GPIO_NUM_21  // Onboard LED (or WS2812 pin)

#elif defined(CONFIG_IDF_TARGET_ESP32)
// =========================================================================
// Target: ESP32 Classic (ESP32-WROOM-32 / NodeMCU / DevKitV1)
// =========================================================================
#define BOARD_TARGET_NAME "ESP32 Classic Gamepad"

#define PIN_UP      GPIO_NUM_13
#define PIN_DOWN    GPIO_NUM_12
#define PIN_LEFT    GPIO_NUM_14
#define PIN_RIGHT   GPIO_NUM_27
#define PIN_A       GPIO_NUM_26
#define PIN_B       GPIO_NUM_25
#define PIN_X       GPIO_NUM_33
#define PIN_Y       GPIO_NUM_32
#define PIN_L       GPIO_NUM_18
#define PIN_R       GPIO_NUM_19
#define PIN_SELECT  GPIO_NUM_22
#define PIN_START   GPIO_NUM_23
#define PIN_MENU    GPIO_NUM_21
#define PIN_LED     GPIO_NUM_2   // Onboard Blue LED on NodeMCU-32S

#else
#error "Unsupported target chip for wireless gamepad!"
#endif
