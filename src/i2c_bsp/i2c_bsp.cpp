#include <stdio.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include "i2c_bsp.h"

static uint32_t i2c_data_pdMS_TICKS = 0;
static uint32_t i2c_done_pdMS_TICKS = 0;

I2cMasterBus *I2cMasterBus::instance_ = NULL;

I2cMasterBus *I2cMasterBus::requestInstance(int scl_pin, int sda_pin, int i2c_port) {
    if (instance_ == NULL) {
        instance_ = new I2cMasterBus(scl_pin, sda_pin, i2c_port);
    }
    return instance_;
}

I2cMasterBus::I2cMasterBus(int scl_pin, int sda_pin, int i2c_port) {
    i2c_data_pdMS_TICKS = pdMS_TO_TICKS(5000);
    i2c_done_pdMS_TICKS = pdMS_TO_TICKS(1000);

    i2c_master_bus_config_t i2c_bus_config      = {};
    i2c_bus_config.clk_source                   = I2C_CLK_SRC_DEFAULT;
    i2c_bus_config.i2c_port                     = (i2c_port_t)i2c_port;
    i2c_bus_config.scl_io_num                   = (gpio_num_t)scl_pin;
    i2c_bus_config.sda_io_num                   = (gpio_num_t)sda_pin;
    i2c_bus_config.glitch_ignore_cnt            = 7;
    i2c_bus_config.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &user_i2c_handle));
}

I2cMasterBus::~I2cMasterBus() {
}

int I2cMasterBus::i2c_probe_addr(uint8_t addr) {
    i2c_master_bus_wait_all_done(user_i2c_handle, i2c_done_pdMS_TICKS);
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = addr;
    dev_cfg.scl_speed_hz = 400000;
    i2c_master_dev_handle_t temp_dev = NULL;
    esp_err_t ret = i2c_master_bus_add_device(user_i2c_handle, &dev_cfg, &temp_dev);
    if (ret != ESP_OK) return ret;
    uint8_t data = 0;
    uint8_t reg = 0x00;
    ret = i2c_master_transmit_receive(temp_dev, &reg, 1, &data, 1, i2c_data_pdMS_TICKS);
    i2c_master_bus_rm_device(temp_dev);
    return ret;
}

int I2cMasterBus::i2c_write_buff(i2c_master_dev_handle_t dev_handle, int reg, uint8_t *buf, uint8_t len) {
    int ret = i2c_master_bus_wait_all_done(user_i2c_handle, i2c_done_pdMS_TICKS);
    if (ret != ESP_OK) return ret;
    if (reg == -1) {
        return i2c_master_transmit(dev_handle, buf, len, i2c_data_pdMS_TICKS);
    }
    uint8_t *pbuf = (uint8_t *)malloc(len + 1);
    pbuf[0] = reg;
    for (uint8_t i = 0; i < len; i++) pbuf[i + 1] = buf[i];
    ret = i2c_master_transmit(dev_handle, pbuf, len + 1, i2c_data_pdMS_TICKS);
    free(pbuf);
    return ret;
}

int I2cMasterBus::i2c_master_write_read_dev(i2c_master_dev_handle_t dev_handle, uint8_t *writeBuf, uint8_t writeLen, uint8_t *readBuf, uint8_t readLen) {
    int ret = i2c_master_bus_wait_all_done(user_i2c_handle, i2c_done_pdMS_TICKS);
    if (ret != ESP_OK) return ret;
    return i2c_master_transmit_receive(dev_handle, writeBuf, writeLen, readBuf, readLen, i2c_data_pdMS_TICKS);
}

int I2cMasterBus::i2c_read_buff(i2c_master_dev_handle_t dev_handle, int reg, uint8_t *buf, uint8_t len) {
    int ret = i2c_master_bus_wait_all_done(user_i2c_handle, i2c_done_pdMS_TICKS);
    if (ret != ESP_OK) return ret;
    if (reg == -1) {
        return i2c_master_receive(dev_handle, buf, len, i2c_data_pdMS_TICKS);
    }
    uint8_t addr = (uint8_t)reg;
    return i2c_master_transmit_receive(dev_handle, &addr, 1, buf, len, i2c_data_pdMS_TICKS);
}

i2c_master_bus_handle_t I2cMasterBus::Get_I2cBusHandle() {
    return user_i2c_handle;
}
