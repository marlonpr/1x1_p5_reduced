#ifndef DS3231_H
#define DS3231_H

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;   // e.g., 2025
    uint8_t  month;  // 1-12
    uint8_t  day;    // 1-31
    uint8_t  hour;   // 0-23
    uint8_t  minute; // 0-59
    uint8_t  second; // 0-59
    uint8_t  day_of_week; // 1=Sunday ... 7=Saturday
} ds3231_time_t;


typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
} ds3231_dev_t;

/**
 * @brief Initialize DS3231 and I2C bus (SDA=21, SCL=22, 100kHz)
 */
esp_err_t init_ds3231(ds3231_dev_t *out_dev);

/**
 * @brief Set time
 */
esp_err_t ds3231_set_time(ds3231_dev_t *dev, const ds3231_time_t *time);

/**
 * @brief Get time
 */
esp_err_t ds3231_get_time(ds3231_dev_t *dev, ds3231_time_t *time);

#ifdef __cplusplus
}
#endif

#endif // DS3231_H
