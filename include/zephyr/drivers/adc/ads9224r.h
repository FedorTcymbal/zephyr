/*
 * Copyright (c) 2025 Custom Company
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API for ADS9224R ADC driver
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_ADC_ADS9224R_H_
#define ZEPHYR_INCLUDE_DRIVERS_ADC_ADS9224R_H_

#include <zephyr/drivers/adc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ADS9224R operational modes
 */
enum ads9224r_mode {
	/** Idle mode - low power, no conversions */
	ADS9224R_MODE_IDLE = 0,
	/** Continuous conversion mode */
	ADS9224R_MODE_CONTINUOUS = 1,
	/** Single-shot conversion mode */
	ADS9224R_MODE_SINGLE = 2,
	/** Sleep mode - lowest power consumption */
	ADS9224R_MODE_SLEEP = 3,
};

/**
 * @brief ADS9224R data rates
 */
enum ads9224r_data_rate {
	/** 125 SPS */
	ADS9224R_DATA_RATE_125_SPS = 0,
	/** 250 SPS */
	ADS9224R_DATA_RATE_250_SPS = 1,
	/** 500 SPS */
	ADS9224R_DATA_RATE_500_SPS = 2,
	/** 1000 SPS */
	ADS9224R_DATA_RATE_1000_SPS = 3,
	/** 2000 SPS */
	ADS9224R_DATA_RATE_2000_SPS = 4,
	/** 4000 SPS */
	ADS9224R_DATA_RATE_4000_SPS = 5,
	/** 8000 SPS */
	ADS9224R_DATA_RATE_8000_SPS = 6,
	/** 16000 SPS */
	ADS9224R_DATA_RATE_16000_SPS = 7,
};

/**
 * @brief Configure the operation mode of ADS9224R
 *
 * This function configures the operating mode of the ADS9224R ADC.
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @param mode Operation mode to set.
 *
 * @retval 0 If successful.
 * @retval -EINVAL If the requested mode is not valid.
 * @retval -EIO General input/output error.
 */
int ads9224r_set_mode(const struct device *dev, enum ads9224r_mode mode);

/**
 * @brief Set the data rate for ADS9224R
 *
 * This function sets the data rate for ADS9224R ADC conversions.
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @param rate Data rate to set.
 *
 * @retval 0 If successful.
 * @retval -EINVAL If the requested rate is not valid.
 * @retval -EIO General input/output error.
 */
int ads9224r_set_data_rate(const struct device *dev, enum ads9224r_data_rate rate);

/**
 * @brief Get the device status
 *
 * This function reads the status register of the ADS9224R ADC.
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @param status Pointer to store the status register value.
 *
 * @retval 0 If successful.
 * @retval -EIO General input/output error.
 */
int ads9224r_get_status(const struct device *dev, uint16_t *status);

/**
 * @brief Reset the ADS9224R device
 *
 * This function performs a software reset of the ADS9224R ADC.
 *
 * @param dev Pointer to the device structure for the driver instance.
 *
 * @retval 0 If successful.
 * @retval -EIO General input/output error.
 */
int ads9224r_reset(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_ADC_ADS9224R_H_ */
