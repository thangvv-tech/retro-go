// ES8311 codec init driver for retro-go
// Used by i2s.c when RG_AUDIO_USE_ES8311 is defined in config.h

#pragma once

#if defined(RG_AUDIO_USE_ES8311) && defined(RG_GPIO_I2C_SDA) && defined(RG_GPIO_I2C_SCL)

#include "rg_system.h"
#include "rg_i2c.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define ES8311_ADDR_DEFAULT     0x18  // CE pin = 0 (GND)

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
#define ES8311_REG_DAC_VCTRL    0x32  // DAC volume: 0x00 = -96dB (mute), 0xBF = 0dB
#define ES8311_REG_DAC_MUTE     0x31
#define ES8311_REG_ADC_EQ       0x1C
#define ES8311_REG_DAC_EQ       0x37
#define ES8311_REG_CHIP_ID1     0xFD
#define ES8311_REG_CHIP_ID2     0xFE
#define ES8311_REG_CHIP_VER     0xFF

// SDP word length: 16-bit = 0b11 at bits[3:2] (3 << 2 = 0x0C)
#define ES8311_SDP_16BIT        0x0C

static uint8_t es8311_i2c_addr = ES8311_ADDR_DEFAULT;

static inline bool es8311_wr(uint8_t reg, uint8_t val)
{
    return rg_i2c_write_byte(es8311_i2c_addr, reg, val);
}

static inline int es8311_rd(uint8_t reg)
{
    return rg_i2c_read_byte(es8311_i2c_addr, reg);
}

// Configure clock dividers assuming MCLK = 256 * sample_rate from ESP32 I2S master
static inline bool es8311_config_clock(int sample_rate)
{
    (void)sample_rate;

    bool ok = true;
    ok &= es8311_wr(ES8311_REG_CLK_PRE,  0x00);
    ok &= es8311_wr(ES8311_REG_ADC_OSR,  0x10);
    ok &= es8311_wr(ES8311_REG_DAC_OSR,  0x10);
    ok &= es8311_wr(ES8311_REG_CLK_DIV,  0x00);
    ok &= es8311_wr(ES8311_REG_CLK_BCLK, 0x03); // bclk_div = 4 -> reg val 3
    ok &= es8311_wr(ES8311_REG_LRCK_H,   0x00);
    ok &= es8311_wr(ES8311_REG_LRCK_L,   0xFF);

    return ok;
}

static inline bool es8311_init(int sample_rate)
{
    if (!rg_i2c_init())
    {
        RG_LOGE("ES8311: I2C init failed!\n");
        return false;
    }

    es8311_i2c_addr = ES8311_ADDR_DEFAULT;

    // Reset chip (try 0x18 first, then fallback to 0x19)
    if (!es8311_wr(ES8311_REG_RESET, 0x1F))
    {
        es8311_i2c_addr = 0x19;
        if (!es8311_wr(ES8311_REG_RESET, 0x1F))
        {
            RG_LOGE("ES8311 not responding on I2C (tried 0x18, 0x19)!\n");
            es8311_i2c_addr = ES8311_ADDR_DEFAULT;
            return false;
        }
        RG_LOGI("ES8311 detected at alternate I2C 0x19\n");
    }
    else
    {
        RG_LOGI("ES8311 detected at I2C 0x18\n");
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    es8311_wr(ES8311_REG_RESET, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));
    // Start CSM (Clock State Machine power on: bit 7 = 1)
    es8311_wr(ES8311_REG_RESET, 0x80);

    // Enable internal clocks, MCLK from MCLK pin
    es8311_wr(ES8311_REG_CLK_MAN1, 0x3F);

    // Clock dividers for MCLK = 256 * fs
    es8311_config_clock(sample_rate);

    // Slave mode (bit 6 = 0) + CSM on (bit 7 = 1)
    es8311_wr(ES8311_REG_RESET, 0x80);

    // Standard Philips 16-bit I2S format
    es8311_wr(ES8311_REG_SDP_IN,  ES8311_SDP_16BIT);
    es8311_wr(ES8311_REG_SDP_OUT, ES8311_SDP_16BIT);

    // Power up analog circuitry & DAC path
    es8311_wr(ES8311_REG_SYSTEM,   0x01); // Power up analog circuitry
    es8311_wr(ES8311_REG_ADC_MUTE, 0x02); // Enable analog PGA & ADC modulator
    es8311_wr(ES8311_REG_DAC_SET1, 0x00); // Power up DAC
    es8311_wr(ES8311_REG_DAC_SET2, 0x10); // Enable output to HP / PA drive

    // EQ bypass
    es8311_wr(ES8311_REG_ADC_EQ, 0x6A);
    es8311_wr(ES8311_REG_DAC_EQ, 0x08);

    // Set DAC hardware volume to 0 dB (0xBF = 0dB unity gain)
    es8311_wr(ES8311_REG_DAC_VCTRL, 0xBF);

    // Unmute DAC output (clear bits 6 and 5)
    es8311_wr(ES8311_REG_DAC_MUTE, 0x00);

    RG_LOGI("ES8311 codec initialized (addr=0x%02X, rate=%d).\n", es8311_i2c_addr, sample_rate);
    return true;
}

static inline bool es8311_set_volume(int percent)
{
    if (percent <= 0)
        return es8311_wr(ES8311_REG_DAC_VCTRL, 0x00);
    if (percent > 100)
        percent = 100;
    uint8_t val = (uint8_t)(percent * 0xBF / 100);
    return es8311_wr(ES8311_REG_DAC_VCTRL, val);
}

static inline bool es8311_set_mute(bool mute)
{
    return es8311_wr(ES8311_REG_DAC_MUTE, mute ? 0x60 : 0x00);
}

#endif // RG_AUDIO_USE_ES8311
