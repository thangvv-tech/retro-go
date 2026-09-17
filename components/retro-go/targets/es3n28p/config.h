// Target definition
#define RG_TARGET_NAME              "ES3N28P"

// Storage - SDMMC 1-bit mode via GPIO matrix (ESP32-S3)
#define RG_STORAGE_ROOT             "/sd"
#define RG_STORAGE_SDMMC_HOST       SDMMC_HOST_SLOT_1
#define RG_STORAGE_SDMMC_SPEED      SDMMC_FREQ_DEFAULT
// #define RG_STORAGE_FLASH_PARTITION  "vfs"

// Audio - I2S external DAC (ES8311)
#define RG_AUDIO_USE_INT_DAC        0   // 0 = Disable, 1 = GPIO25, 2 = GPIO26, 3 = Both
#define RG_AUDIO_USE_EXT_DAC        1   // 0 = Disable, 1 = Enable

// Video - ILI9341V, 2.8\" 320x240 IPS via SPI2
#define RG_SCREEN_DRIVER            0   // 0 = ILI9341/ST7789
#define RG_SCREEN_HOST              SPI2_HOST
#define RG_SCREEN_SPEED             SPI_MASTER_FREQ_40M
#define RG_SCREEN_BACKLIGHT         1
#define RG_SCREEN_WIDTH             320
#define RG_SCREEN_HEIGHT            240
#define RG_SCREEN_ROTATE            0
#define RG_SCREEN_VISIBLE_AREA      {0, 0, 0, 0}
#define RG_SCREEN_SAFE_AREA         {0, 0, 0, 0}
#define RG_SCREEN_INIT() \
    ILI9341_CMD(0xCF, 0x00, 0xC1, 0x30); \
    ILI9341_CMD(0xED, 0x64, 0x03, 0x12, 0x81); \
    ILI9341_CMD(0xE8, 0x85, 0x00, 0x78); \
    ILI9341_CMD(0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02); \
    ILI9341_CMD(0xF7, 0x20); \
    ILI9341_CMD(0xEA, 0x00, 0x00); \
    ILI9341_CMD(0xC0, 0x13); \
    ILI9341_CMD(0xC1, 0x13); \
    ILI9341_CMD(0xC5, 0x22, 0x35); \
    ILI9341_CMD(0xC7, 0xBD); \
    ILI9341_CMD(0x21); \
    ILI9341_CMD(0x36, 0x68); \
    ILI9341_CMD(0xB6, 0x0A, 0xA2); \
    ILI9341_CMD(0x3A, 0x55); \
    ILI9341_CMD(0xF6, 0x01, 0x30); \
    ILI9341_CMD(0xB1, 0x00, 0x1B); \
    ILI9341_CMD(0xF2, 0x00); \
    ILI9341_CMD(0x26, 0x01); \
    ILI9341_CMD(0xE0, 0x0F, 0x35, 0x31, 0x0B, 0x0E, 0x06, 0x49, 0xA7, 0x33, 0x07, 0x0F, 0x03, 0x0C, 0x0A, 0x00); \
    ILI9341_CMD(0xE1, 0x00, 0x0A, 0x0F, 0x04, 0x11, 0x08, 0x36, 0x58, 0x4D, 0x07, 0x10, 0x0C, 0x32, 0x34, 0x0F);

// Gamepad - All buttons via PCF8574 (I2C), IO0=Secondary MENU (onboard BOOT button)
#define RG_I2C_GPIO_DRIVER      5       // PCF8574
#define RG_I2C_GPIO_ADDR        0x20    // A0/A1/A2 = GND

#define RG_GAMEPAD_I2C_MAP { \
    {RG_KEY_UP,     .num = 0, .level = 0}, \
    {RG_KEY_DOWN,   .num = 1, .level = 0}, \
    {RG_KEY_LEFT,   .num = 2, .level = 0}, \
    {RG_KEY_RIGHT,  .num = 3, .level = 0}, \
    {RG_KEY_A,      .num = 4, .level = 0}, \
    {RG_KEY_B,      .num = 5, .level = 0}, \
    {RG_KEY_SELECT, .num = 6, .level = 0}, \
    {RG_KEY_START,  .num = 7, .level = 0}, \
}

#define RG_GAMEPAD_VIRT_MAP { \
    {RG_KEY_MENU,   .src = RG_KEY_SELECT | RG_KEY_START}, \
    {RG_KEY_OPTION, .src = RG_KEY_SELECT | RG_KEY_A}, \
}

// Wireless Gamepad - ESP-NOW Receiver (P2P 2.4GHz for ESP32-C3 SuperMini)
#define RG_GAMEPAD_USE_ESPNOW       1

// Battery - IO9 ADC (200k/200k voltage divider on board -> 2:1 ratio)
#define RG_BATTERY_DRIVER           1
#define RG_BATTERY_ADC_UNIT         ADC_UNIT_1
#define RG_BATTERY_ADC_CHANNEL      ADC_CHANNEL_8   // IO9 = ADC1_CH8 on ESP32-S3
#define RG_BATTERY_CALC_PERCENT(raw) (((raw) * 2.f - 3500.f) / (4200.f - 3500.f) * 100.f)
#define RG_BATTERY_CALC_VOLTAGE(raw) ((raw) * 2.f * 0.001f)

// Status LED - WS2812B RGB LED on IO42 (disabled: requires RMT driver, not simple GPIO)
// #define RG_GPIO_LED                 GPIO_NUM_42

// SPI Display (ILI9341V)
#define RG_GPIO_LCD_MISO            GPIO_NUM_13
#define RG_GPIO_LCD_MOSI            GPIO_NUM_11
#define RG_GPIO_LCD_CLK             GPIO_NUM_12
#define RG_GPIO_LCD_CS              GPIO_NUM_10
#define RG_GPIO_LCD_DC              GPIO_NUM_46
#define RG_GPIO_LCD_BCKL            GPIO_NUM_45
// RG_GPIO_LCD_RST not defined — no RST pin wired (board uses EN for hardware reset)

// SDMMC (GPIO matrix on ESP32-S3)
// Uses RG_GPIO_SDSPI_* names as the storage driver reads them for SDMMC GPIO matrix mode
#define RG_GPIO_SDSPI_CLK           GPIO_NUM_38
#define RG_GPIO_SDSPI_CMD           GPIO_NUM_40
#define RG_GPIO_SDSPI_D0            GPIO_NUM_39
#define RG_GPIO_SDSPI_D1            GPIO_NUM_41
#define RG_GPIO_SDSPI_D2            GPIO_NUM_48
#define RG_GPIO_SDSPI_D3            GPIO_NUM_47

// I2C bus (used by ES8311 codec & PCF8574 gamepad)
#define RG_GPIO_I2C_SDA             GPIO_NUM_16
#define RG_GPIO_I2C_SCL             GPIO_NUM_15

// I2S Audio with ES8311 codec + SC8002B amplifier
#define RG_AUDIO_USE_ES8311         1
#define RG_GPIO_SND_I2S_MCLK        GPIO_NUM_4
#define RG_GPIO_SND_I2S_BCK         GPIO_NUM_5
#define RG_GPIO_SND_I2S_WS          GPIO_NUM_7
#define RG_GPIO_SND_I2S_DATA        GPIO_NUM_6
// IO1: SC8002B SHUTDOWN pin (Low = enable amp, High = disable) -> use INVERT flag
#define RG_GPIO_SND_AMP_ENABLE      GPIO_NUM_1
#define RG_GPIO_SND_AMP_ENABLE_INVERT
