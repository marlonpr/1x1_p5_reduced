#include "ds3231.h"
#include "esp_log.h"

#define SDA_PIN 21
#define SCL_PIN 22
#define DS3231_ADDR 0x68
#define TAG "DS3231"

// BCD helpers
static inline uint8_t dec2bcd(uint8_t val) { return ((val/10)<<4) | (val%10); }
static inline uint8_t bcd2dec(uint8_t val) { return ((val>>4)*10) + (val&0x0F); }

esp_err_t init_ds3231(ds3231_dev_t *out_dev)
{
    if (!out_dev) return ESP_ERR_INVALID_ARG;

    // 1. Configure I2C bus
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &out_dev->bus));

    // 2. Add DS3231 device
    i2c_device_config_t dev_cfg = {
        .device_address = DS3231_ADDR,
        .scl_speed_hz = 100000
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(out_dev->bus, &dev_cfg, &out_dev->dev));

    ESP_LOGI(TAG, "DS3231 initialized on SDA=%d, SCL=%d", SDA_PIN, SCL_PIN);
    return ESP_OK;
}

esp_err_t ds3231_set_time(ds3231_dev_t *dev, const ds3231_time_t *time)
{
    if (!dev || !time) return ESP_ERR_INVALID_ARG;

    uint8_t buf[8];
    buf[0] = 0x00; // start register
    buf[1] = dec2bcd(time->second);
    buf[2] = dec2bcd(time->minute);
    buf[3] = dec2bcd(time->hour & 0x3F);
    buf[4] = dec2bcd(time->day_of_week & 0x07); // <-- set correctly (1–7)
    buf[5] = dec2bcd(time->day);
    buf[6] = dec2bcd(time->month);
    buf[7] = dec2bcd(time->year % 100);

    return i2c_master_transmit(dev->dev, buf, sizeof(buf), -1);
}


esp_err_t ds3231_get_time(ds3231_dev_t *dev, ds3231_time_t *time)
{
    if (!dev || !time) return ESP_ERR_INVALID_ARG;

    uint8_t reg = 0x00;
    uint8_t data[7];

    ESP_ERROR_CHECK(i2c_master_transmit(dev->dev, &reg, 1, -1));
    ESP_ERROR_CHECK(i2c_master_receive(dev->dev, data, 7, -1));

    time->second      = bcd2dec(data[0] & 0x7F);
    time->minute      = bcd2dec(data[1] & 0x7F);
    time->hour        = bcd2dec(data[2] & 0x3F);
    time->day_of_week = bcd2dec(data[3] & 0x07); // 1–7
    time->day         = bcd2dec(data[4] & 0x3F);
    time->month       = bcd2dec(data[5] & 0x1F);
    time->year        = 2000 + bcd2dec(data[6]);

    return ESP_OK;
}


