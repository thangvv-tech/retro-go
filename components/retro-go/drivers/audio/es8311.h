// ES8311 codec init driver for retro-go
// Used by i2s.c when RG_AUDIO_USE_ES8311 is defined in config.h

#pragma once

#if defined(RG_AUDIO_USE_ES8311) && defined(RG_GPIO_I2C_SDA) && defined(RG_GPIO_I2C_SCL)

#include "rg_system.h"
#include "rg_i2c.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define ES8311_ADDR             0x18  // ADDR pin = 0 (GND)

// Register addresses
#define ES8311_REG_RESET        0x00
#define ES8311_REG_CLK_MAN1     0x01
#define ES8311_REG_CLK_PRE      0x02
#define ES8311_REG_ADC_OSR      0x03
#define ES8311_REG_DAC_OSR      0x04
#define ES8311_REG_CLK_DIV      0x05
#define ES8311_REG_CLK_BCLK     0x06
#define ES8311_REG_LRCK_H       0x07
#define ES8311_REG_LRCK_L       0x08
#define ES8311_REG_SDP_IN       0x09  // I2S receive format (DAC input)
#define ES8311_REG_SDP_OUT      0x0A  // I2S transmit format (ADC output)
#define ES8311_REG_SYSTEM       0x0D
#define ES8311_REG_ADC_MUTE     0x0E
#define ES8311_REG_DAC_SET1     0x12
#define ES8311_REG_DAC_SET2     0x13
#define ES8311_REG_DAC_VCTRL    0x32  // DAC volume 0x00=0dB, 0xFF=-96dB
#define ES8311_REG_DAC_MUTE     0x31
#define ES8311_REG_ADC_EQ       0x1C
#define ES8311_REG_DAC_EQ       0x37

// SDP word length: 16-bit = 0b11 at bits[3:2]
#define ES8311_SDP_16BIT        0x0C

static inline bool es8311_wr(uint8_t reg, uint8_t val)
{
    return rg_i2c_write_byte(ES8311_ADDR, reg, val);
}

static inline int es8311_rd(uint8_t reg)
{
    return rg_i2c_read_byte(ES8311_ADDR, reg);
}

// Compute clock dividers for given sample_rate assuming MCLK = 256 * sample_rate
static inline bool es8311_config_clock(int sample_rate)
{
    uint8_t pre = 0x00;    // REG02: pre_div=1, pre_multi=1
    uint8_t adc_osr = 0x00;
    uint8_t dac_osr = 0x00;
    uint8_t clk_div = 0x00; // REG05: adc_div=1, dac_div=1
    uint8_t bclk_div;       // REG06
    uint16_t lrck;          // REG07/08

    // MCLK = 256*fs (configured by ESP32 I2S MCLK output)
    // BCLK = 32*fs (16bit * 2ch), lrck = fs
    // bclk_div = 256/32 = 8
    bclk_div = 8 - 1; // = 7
    lrck = 64;         // BCLK ticks per LRCK period = 32*2 = 64

    es8311_wr(ES8311_REG_CLK_PRE, pre);
    es8311_wr(ES8311_REG_ADC_OSR, adc_osr);
    es8311_wr(ES8311_REG_DAC_OSR, dac_osr);
    es8311_wr(ES8311_REG_CLK_DIV, clk_div);
    es8311_wr(ES8311_REG_CLK_BCLK, bclk_div);
    es8311_wr(ES8311_REG_LRCK_H, (lrck >> 8) & 0xFF);
    es8311_wr(ES8311_REG_LRCK_L, lrck & 0xFF);

    return true;
}

static inline bool es8311_init(int sample_rate)
{
    if (!rg_i2c_init())
        return false;

    // Reset
    es8311_wr(ES8311_REG_RESET, 0x1F);
    vTaskDelay(pdMS_TO_TICKS(10));
    es8311_wr(ES8311_REG_RESET, 0x00);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Enable all clocks, MCLK from MCLK pin (not from BCLK)
    es8311_wr(ES8311_REG_CLK_MAN1, 0x3F);

    // Slave mode (bit6=0)
    int r = es8311_rd(ES8311_REG_RESET);
    if (r < 0) r = 0x80;
    es8311_wr(ES8311_REG_RESET, (uint8_t)(r & 0xBF));

    es8311_config_clock(sample_rate);

    // I2S: 16-bit standard (Philips) for both Rx (DAC) and Tx (ADC)
    es8311_wr(ES8311_REG_SDP_IN,  ES8311_SDP_16BIT);
    es8311_wr(ES8311_REG_SDP_OUT, ES8311_SDP_16BIT);

    // Analog power, DAC path
    es8311_wr(ES8311_REG_SYSTEM,   0x01); // power analog section
    es8311_wr(ES8311_REG_ADC_MUTE, 0x02); // ADC modulator on but muted (not using mic)
    es8311_wr(ES8311_REG_DAC_SET1, 0x00); // power up DAC
    es8311_wr(ES8311_REG_DAC_SET2, 0x10); // enable HP driver output

    // EQ bypass
    es8311_wr(ES8311_REG_ADC_EQ, 0x6A);
    es8311_wr(ES8311_REG_DAC_EQ, 0x08);

    // Default volume: 0 dB
    es8311_wr(ES8311_REG_DAC_VCTRL, 0x00);

    // Unmute DAC output
    int rv = es8311_rd(ES8311_REG_DAC_MUTE);
    if (rv < 0) rv = 0;
    es8311_wr(ES8311_REG_DAC_MUTE, (uint8_t)(rv & ~(BIT(6) | BIT(5))));

    RG_LOGI("ES8311 codec initialized (rate=%d).\n", sample_rate);
    return true;
}

static inline bool es8311_set_volume(int percent)
{
    uint8_t val = (uint8_t)((100 - percent) * 0xBF / 100);
    return es8311_wr(ES8311_REG_DAC_VCTRL, val);
}

static inline bool es8311_set_mute(bool mute)
{
    int rv = es8311_rd(ES8311_REG_DAC_MUTE);
    if (rv < 0) return false;
    uint8_t v = (uint8_t)rv;
    if (mute)
        v |= (BIT(6) | BIT(5));
    else
        v &= ~(BIT(6) | BIT(5));
    return es8311_wr(ES8311_REG_DAC_MUTE, v);
}

#endif // RG_AUDIO_USE_ES8311
