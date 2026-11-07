#include "power_status.h"

#include "bsp/m5stack_tab5.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <stdbool.h>

/* Second PI4IOE5V6408 (0x44), set up as M5Unified does for Tab5:
 * P5 nCHG_QC_EN (output, low selects the 1000 mA step, high 500 mA),
 * P6 IP2326 BAT_STAT (input; high while charging and while disabled),
 * P7 CHG_EN (output, high = charger on).
 * After reset every pin is high-Z with pull-down, so the charger stays off
 * until firmware drives CHG_EN high. */
#define POWER_CHG_QC   IO_EXPANDER_PIN_NUM_5
#define POWER_CHG_STAT IO_EXPANDER_PIN_NUM_6
#define POWER_CHG_EN   IO_EXPANDER_PIN_NUM_7

/* INA226 on the battery rail, 5 mOhm shunt; charge current reads negative. */
#define INA226_ADDRESS      0x41
#define INA226_REG_SHUNT    0x01
#define INA226_REG_BUS      0x02
#define INA226_BUS_LSB_UV   1250
#define INA226_SHUNT_LSB_NV 2500
#define INA226_SHUNT_MOHM   5

static const char *TAG = "POWER";
static esp_io_expander_handle_t s_expander;
static i2c_master_dev_handle_t s_ina226;
static bool s_charge_on;

static esp_err_t ina226_read(uint8_t reg, uint16_t *value)
{
    uint8_t data[2];
    esp_err_t err = i2c_master_transmit_receive(s_ina226, &reg, 1, data, sizeof(data), 50);
    if (err == ESP_OK) {
        *value = (uint16_t)((data[0] << 8) | data[1]);
    }
    return err;
}

void tab5_power_log(void)
{
    /* The input register does not echo output pins, so report what was set. */
    const int chg_en = s_charge_on ? 1 : 0;
    uint32_t levels = 0;
    int chg_stat = -1;
    if (s_expander && esp_io_expander_get_level(s_expander, POWER_CHG_STAT, &levels) == ESP_OK) {
        chg_stat = (levels & POWER_CHG_STAT) ? 1 : 0;
    }
    uint16_t bus = 0;
    uint16_t shunt = 0;
    if (!s_ina226 || ina226_read(INA226_REG_BUS, &bus) != ESP_OK || ina226_read(INA226_REG_SHUNT, &shunt) != ESP_OK) {
        ESP_LOGW(TAG, "STATUS chg_en=%d chg_stat=%d battery=unreadable", chg_en, chg_stat);
        return;
    }
    const unsigned battery_mv = (unsigned)bus * INA226_BUS_LSB_UV / 1000;
    const int shunt_uv = (int)(int16_t)shunt * INA226_SHUNT_LSB_NV / 1000;
    const int charge_ma = -shunt_uv / INA226_SHUNT_MOHM;
    ESP_LOGI(TAG, "STATUS chg_en=%d bat_stat=%d battery_mv=%u charge_ma=%d", chg_en, chg_stat, battery_mv, charge_ma);
}

esp_err_t tab5_power_init(void)
{
    s_expander = bsp_io_expander1_init();
    if (!s_expander) {
        ESP_LOGE(TAG, "IO expander 0x44 unavailable; charger left off");
    }
    const i2c_device_config_t ina226_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = INA226_ADDRESS,
        .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(bsp_i2c_get_handle(), &ina226_config, &s_ina226) != ESP_OK) {
        s_ina226 = NULL;
    }
    tab5_power_log();
    if (s_expander) {
        esp_err_t err = esp_io_expander_set_dir(s_expander, POWER_CHG_STAT, IO_EXPANDER_INPUT);
        err |= esp_io_expander_set_dir(s_expander, POWER_CHG_QC, IO_EXPANDER_OUTPUT);
        err |= esp_io_expander_set_level(s_expander, POWER_CHG_QC, 0);
        err |= esp_io_expander_set_output_mode(s_expander, POWER_CHG_QC, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
        err |= esp_io_expander_set_dir(s_expander, POWER_CHG_EN, IO_EXPANDER_OUTPUT);
        err |= esp_io_expander_set_level(s_expander, POWER_CHG_EN, 1);
        err |= esp_io_expander_set_output_mode(s_expander, POWER_CHG_EN, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "charger enable failed");
        } else {
            s_charge_on = true;
            ESP_LOGI(TAG, "charger enabled");
        }
    }
    tab5_power_log();
    return ESP_OK;
}
