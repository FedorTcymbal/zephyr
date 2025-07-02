/*
 * Copyright (c) 2025 Custom Company
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Sample for ADS9224R ADC driver
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/ads9224r.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define ADC_NODE DT_ALIAS(adc_ads9224r)

#if !DT_NODE_EXISTS(ADC_NODE)
#error "ADC device not found in device tree"
#endif

/* Number of samples to take for each channel */
#define SAMPLE_COUNT 10

/* Maximum value for 16-bit ADC (2^16 - 1) */
#define ADC_MAX_VALUE 65535

/* Buffer to store ADC samples */
static uint32_t sample_buffer[SAMPLE_COUNT];

/* ADC channels configuration */
static const struct adc_channel_cfg channel_cfg = {
    .gain = ADC_GAIN_1,
    .reference = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME_DEFAULT,
    .channel_id = 0,
};

/* ADC sequence configuration */
static const struct adc_sequence sequence = {
    .channels = BIT(0),
    .buffer = sample_buffer,
    .buffer_size = sizeof(sample_buffer),
    .resolution = 16,
    .oversampling = 0,
    .calibrate = false,
};

void main(void)
{
    const struct device *const dev = DEVICE_DT_GET(ADC_NODE);
    int ret;
    int32_t value_mv;

    if (!device_is_ready(dev)) {
        printk("ADS9224R device not ready\n");
        return;
    }

    printk("ADS9224R ADC example started\n");

    /* Configure ADC channel */
    ret = adc_channel_setup(dev, &channel_cfg);
    if (ret != 0) {
        printk("Failed to configure ADC channel (err %d)\n", ret);
        return;
    }

    /* Set data rate for ADC conversions */
    ret = ads9224r_set_data_rate(dev, ADS9224R_DATA_RATE_1000_SPS);
    if (ret != 0) {
        printk("Failed to set data rate (err %d)\n", ret);
        return;
    }

    /* Set operational mode for the ADC */
    ret = ads9224r_set_mode(dev, ADS9224R_MODE_CONTINUOUS);
    if (ret != 0) {
        printk("Failed to set operational mode (err %d)\n", ret);
        return;
    }

    printk("Starting ADC sampling...\n");

    while (1) {
        /* Read ADC samples */
        ret = adc_read(dev, &sequence);
        if (ret != 0) {
            printk("Failed to read ADC samples (err %d)\n", ret);
            continue;
        }

        /* Process samples */
        for (int i = 0; i < SAMPLE_COUNT; i++) {
            /* Convert ADC value to millivolts */
            ret = adc_raw_to_millivolts(adc_ref_internal(dev),
                                      channel_cfg.gain,
                                      sequence.resolution,
                                      &sample_buffer[i],
                                      &value_mv);
            if (ret != 0) {
                printk("Failed to convert ADC value to millivolts (err %d)\n", ret);
                continue;
            }

            printk("Sample %d: raw=%u, %d mV\n", i, sample_buffer[i], value_mv);
        }

        /* Wait before taking the next set of samples */
        k_sleep(K_SECONDS(5));
    }
}
