/*
 * Copyright (c) 2025 Custom Company
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Driver for ADS9224R ADC
 *
 * This driver implements support for the ADS9224R, a high-performance
 * analog-to-digital converter.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#define ADC_CONTEXT_USES_KERNEL_TIMER
#include "adc_context.h"

LOG_MODULE_REGISTER(ADS9224R, CONFIG_ADC_LOG_LEVEL);

/* ADS9224R Register Map */
#define ADS9224R_REG_CONFIG         0x00  /* Configuration Register */
#define ADS9224R_REG_STATUS         0x01  /* Status Register */
#define ADS9224R_REG_DATA           0x02  /* Data Register */
#define ADS9224R_REG_GAIN           0x03  /* Gain Register */
#define ADS9224R_REG_OFFSET         0x04  /* Offset Register */
#define ADS9224R_REG_FILTER         0x05  /* Filter Configuration */
#define ADS9224R_REG_ID             0x0F  /* Device ID Register */

/* ADS9224R Configuration Register Bits */
#define ADS9224R_CONFIG_RESET       BIT(15) /* Software Reset */
#define ADS9224R_CONFIG_START       BIT(14) /* Start Conversion */
#define ADS9224R_CONFIG_MODE_MASK   (0x3 << 12) /* Operating Mode Mask */
#define ADS9224R_CONFIG_MODE_IDLE   (0x0 << 12) /* Idle Mode */
#define ADS9224R_CONFIG_MODE_CONT   (0x1 << 12) /* Continuous Conversion Mode */
#define ADS9224R_CONFIG_MODE_SINGLE (0x2 << 12) /* Single Conversion Mode */
#define ADS9224R_CONFIG_MODE_SLEEP  (0x3 << 12) /* Sleep Mode */
#define ADS9224R_CONFIG_CHANNEL(x)  ((x & 0xF) << 8) /* Channel Selection */
#define ADS9224R_CONFIG_DATARATE(x) ((x & 0x7) << 5) /* Data Rate Selection */
#define ADS9224R_CONFIG_PGA(x)      ((x & 0x7) << 2) /* PGA Gain Selection */
#define ADS9224R_CONFIG_CRC_EN      BIT(1) /* CRC Enable */
#define ADS9224R_CONFIG_DRDYL_EN    BIT(0) /* DRDY Low Enable */

/* ADS9224R Status Register Bits */
#define ADS9224R_STATUS_DRDY        BIT(7) /* Data Ready */
#define ADS9224R_STATUS_ERROR       BIT(6) /* Error Flag */
#define ADS9224R_STATUS_CRC_ERR     BIT(5) /* CRC Error */

/* ADS9224R Device ID */
#define ADS9224R_ID                 0x24

/* Maximum number of channels */
#define ADS9224R_MAX_CHANNELS       4

struct ads9224r_config {
    struct spi_dt_spec bus;
    struct gpio_dt_spec drdy_gpio;
    uint8_t channels;
};

struct ads9224r_data {
    struct adc_context ctx;
    const struct device *dev;
    uint16_t buffer;
    uint16_t *read_buf;
    uint16_t *buf_end;
    uint8_t channel_id;
    struct k_sem sem;
    struct k_thread thread;
    enum adc_gain gain;
    enum adc_reference reference;
    
    /* Thread stack */
    K_KERNEL_STACK_MEMBER(stack, CONFIG_ADC_ADS9224R_ACQUISITION_THREAD_STACK_SIZE);
};

/**
 * @brief Write to ADS9224R register
 *
 * @param dev Pointer to the device structure
 * @param reg Register address
 * @param value Value to write
 * @return 0 if successful, negative errno otherwise
 */
static int ads9224r_reg_write(const struct device *dev, uint8_t reg, uint16_t value)
{
    const struct ads9224r_config *config = dev->config;
    uint8_t tx_buffer[3];
    
    /* Command byte: Write operation (MSB = 0) + register address */
    tx_buffer[0] = reg & 0x7F;
    
    /* Data bytes (MSB first) */
    tx_buffer[1] = (value >> 8) & 0xFF;
    tx_buffer[2] = value & 0xFF;
    
    const struct spi_buf tx_buf = {
        .buf = tx_buffer,
        .len = sizeof(tx_buffer)
    };
    
    const struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count = 1
    };
    
    return spi_write_dt(&config->bus, &tx);
}

/**
 * @brief Read from ADS9224R register
 *
 * @param dev Pointer to the device structure
 * @param reg Register address
 * @param value Pointer to store the read value
 * @return 0 if successful, negative errno otherwise
 */
static int ads9224r_reg_read(const struct device *dev, uint8_t reg, uint16_t *value)
{
    const struct ads9224r_config *config = dev->config;
    uint8_t tx_buffer[3] = {0};
    uint8_t rx_buffer[3] = {0};
    
    /* Command byte: Read operation (MSB = 1) + register address */
    tx_buffer[0] = (reg & 0x7F) | 0x80;
    
    const struct spi_buf tx_buf = {
        .buf = tx_buffer,
        .len = sizeof(tx_buffer)
    };
    
    const struct spi_buf rx_buf = {
        .buf = rx_buffer,
        .len = sizeof(rx_buffer)
    };
    
    const struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count = 1
    };
    
    const struct spi_buf_set rx = {
        .buffers = &rx_buf,
        .count = 1
    };
    
    int ret = spi_transceive_dt(&config->bus, &tx, &rx);
    if (ret == 0) {
        *value = ((uint16_t)rx_buffer[1] << 8) | rx_buffer[2];
    }
    
    return ret;
}

/**
 * @brief Configure ADC channel
 *
 * @param dev Pointer to the device structure
 * @param channel_cfg Channel configuration
 * @return 0 if successful, negative errno otherwise
 */
static int ads9224r_channel_setup(const struct device *dev,
                              const struct adc_channel_cfg *channel_cfg)
{
    struct ads9224r_data *data = dev->data;
    
    if (channel_cfg->channel_id >= ADS9224R_MAX_CHANNELS) {
        LOG_ERR("Channel %d is not valid", channel_cfg->channel_id);
        return -EINVAL;
    }
    
    /* Store gain and reference settings for the channel */
    data->gain = channel_cfg->gain;
    data->reference = channel_cfg->reference;
    
    /* ADS9224R has fixed reference, check if requested reference is supported */
    if (data->reference != ADC_REF_INTERNAL) {
        LOG_ERR("Unsupported ADC reference");
        return -EINVAL;
    }
    
    /* Convert gain setting to device-specific PGA setting */
    uint8_t pga_setting;
    switch (data->gain) {
    case ADC_GAIN_1:
        pga_setting = 0; /* PGA gain = 1 */
        break;
    case ADC_GAIN_2:
        pga_setting = 1; /* PGA gain = 2 */
        break;
    case ADC_GAIN_4:
        pga_setting = 2; /* PGA gain = 4 */
        break;
    case ADC_GAIN_8:
        pga_setting = 3; /* PGA gain = 8 */
        break;
    default:
        LOG_ERR("Unsupported ADC gain %d", data->gain);
        return -EINVAL;
    }
    
    /* Write gain setting to register */
    return ads9224r_reg_write(dev, ADS9224R_REG_GAIN, pga_setting);
}

/**
 * @brief Perform ADC acquisition
 *
 * @param dev Pointer to the device structure
 * @param sequence ADC sequence description
 * @return 0 if successful, negative errno otherwise
 */
static int ads9224r_start_read(const struct device *dev,
                          const struct adc_sequence *sequence)
{
    struct ads9224r_data *data = dev->data;
    int ret;

    if (sequence->resolution != 24) {
        LOG_ERR("Unsupported ADC resolution %d", sequence->resolution);
        return -EINVAL;
    }

    if (sequence->channels & ~BIT_MASK(ADS9224R_MAX_CHANNELS)) {
        LOG_ERR("Unsupported channel in mask: 0x%08x",
            sequence->channels & ~BIT_MASK(ADS9224R_MAX_CHANNELS));
        return -EINVAL;
    }

    data->buffer = 0;
    data->read_buf = sequence->buffer;
    data->buf_end = data->read_buf + sequence->buffer_size / sizeof(uint16_t);

    /* Configure and start conversion */
    uint16_t config = ADS9224R_CONFIG_START | ADS9224R_CONFIG_MODE_SINGLE;
    config |= ADS9224R_CONFIG_CHANNEL(data->channel_id);
    ret = ads9224r_reg_write(dev, ADS9224R_REG_CONFIG, config);
    if (ret != 0) {
        return ret;
    }

    /* Wait for conversion to complete */
    k_sem_take(&data->sem, K_FOREVER);

    /* Read conversion result */
    uint16_t result;
    ret = ads9224r_reg_read(dev, ADS9224R_REG_DATA, &result);
    if (ret != 0) {
        return ret;
    }

    /* Store result in user buffer */
    *data->read_buf = result;

    return 0;
}

/**
 * @brief Handler for ADC DRDY interrupt
 *
 * @param dev Pointer to the device structure
 * @param gpio_cb Pointer to the GPIO callback structure
 * @param pins GPIO pins that triggered the interrupt
 */
static void ads9224r_drdy_callback(const struct device *dev,
                               struct gpio_callback *gpio_cb,
                               uint32_t pins)
{
    struct ads9224r_data *data =
        CONTAINER_OF(gpio_cb, struct ads9224r_data, gpio_cb);

    k_sem_give(&data->sem);
}

/**
 * @brief Initialize ADS9224R device
 *
 * @param dev Pointer to the device structure
 * @return 0 if successful, negative errno otherwise
 */
static int ads9224r_init(const struct device *dev)
{
    const struct ads9224r_config *config = dev->config;
    struct ads9224r_data *data = dev->data;
    uint16_t id_value;
    int ret;

    if (!spi_is_ready_dt(&config->bus)) {
        LOG_ERR("SPI bus not ready");
        return -ENODEV;
    }

    data->dev = dev;
    k_sem_init(&data->sem, 0, 1);

    /* Reset the device */
    ret = ads9224r_reg_write(dev, ADS9224R_REG_CONFIG, ADS9224R_CONFIG_RESET);
    if (ret < 0) {
        LOG_ERR("Failed to reset device");
        return ret;
    }

    /* Allow time for reset to complete */
    k_sleep(K_MSEC(10));

    /* Verify device ID */
    ret = ads9224r_reg_read(dev, ADS9224R_REG_ID, &id_value);
    if (ret < 0) {
        LOG_ERR("Failed to read device ID");
        return ret;
    }

    if ((id_value & 0xFF) != ADS9224R_ID) {
        LOG_ERR("Invalid device ID: 0x%04x", id_value);
        return -ENODEV;
    }

    /* Configure default settings */
    uint16_t config = ADS9224R_CONFIG_MODE_IDLE; /* Start in idle mode */
    ret = ads9224r_reg_write(dev, ADS9224R_REG_CONFIG, config);
    if (ret < 0) {
        LOG_ERR("Failed to configure device");
        return ret;
    }

    /* Configure DRDY GPIO if available */
    if (config->drdy_gpio.port != NULL) {
        if (!gpio_is_ready_dt(&config->drdy_gpio)) {
            LOG_ERR("GPIO port for DRDY not ready");
            return -ENODEV;
        }

        ret = gpio_pin_configure_dt(&config->drdy_gpio, GPIO_INPUT);
        if (ret < 0) {
            LOG_ERR("Failed to configure DRDY GPIO pin");
            return ret;
        }

        /* Configure DRDY interrupt */
        gpio_init_callback(&data->gpio_cb, ads9224r_drdy_callback,
                          BIT(config->drdy_gpio.pin));
        ret = gpio_add_callback(config->drdy_gpio.port, &data->gpio_cb);
        if (ret < 0) {
            LOG_ERR("Failed to add DRDY callback");
            return ret;
        }

        ret = gpio_pin_interrupt_configure_dt(&config->drdy_gpio,
                                           GPIO_INT_EDGE_FALLING);
        if (ret < 0) {
            LOG_ERR("Failed to configure DRDY interrupt");
            return ret;
        }
    }

    LOG_INF("ADS9224R initialized successfully");
    return 0;
}

/* ADC API function pointers */
static const struct adc_driver_api ads9224r_driver_api = {
    .channel_setup = ads9224r_channel_setup,
    .read = ads9224r_start_read,
    .ref_internal = 2500, /* 2.5V internal reference */
};

/* Device instantiation */
#define ADS9224R_INIT(n)                                                \
    static struct ads9224r_data ads9224r_data_##n;                      \
                                                                        \
    static const struct ads9224r_config ads9224r_config_##n = {         \
        .bus = SPI_DT_SPEC_INST_GET(n,                                  \
                   SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_MODE_0,   \
                   0),                                                  \
        .drdy_gpio = GPIO_DT_SPEC_INST_GET_OR(n, drdy_gpios, {0}),      \
        .channels = DT_INST_PROP(n, channels),                          \
    };                                                                  \
                                                                        \
    DEVICE_DT_INST_DEFINE(n,                                            \
              ads9224r_init,                                            \
              NULL,                                                     \
              &ads9224r_data_##n,                                       \
              &ads9224r_config_##n,                                     \
              POST_KERNEL,                                              \
              CONFIG_ADC_INIT_PRIORITY,                                 \
              &ads9224r_driver_api);

/* Create instances for each DT node */
DT_INST_FOREACH_STATUS_OKAY(ADS9224R_INIT)
