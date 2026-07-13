#include <stdio.h>
#include "driver/gpio.h"
#include "esp8266/gpio_struct.h"
#include "production_io.h"

#define DIS_ENB GPIO_NUM_15

#define D1_SCLK GPIO_NUM_14
#define D1_RCLK GPIO_NUM_12
#define D1_SDATA1 GPIO_NUM_13
#define D1_SDATA2 GPIO_NUM_16

#define D2_SCLK GPIO_NUM_4
#define D2_RCLK GPIO_NUM_5
#define D2_SDATA1 GPIO_NUM_2
#define D2_SDATA2 GPIO_NUM_0

static xQueueHandle *ptr_send_que = NULL;

esp_err_t production_io_init(xQueueHandle *send_que)
{
    esp_err_t ret = ESP_OK;
	gpio_config_t io_conf;

	//set display enable pin output and turn off
	io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pin_bit_mask = BIT(DIS_ENB);
	io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
	gpio_config(&io_conf);
    
    //set inputs
    io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = BIT(D1_SCLK) | BIT(D1_RCLK) | BIT(D1_SDATA1) | BIT(D1_SDATA2) | BIT(D2_SCLK) | BIT(D2_RCLK) | BIT(D2_SDATA1) | BIT(D2_SDATA2);
	io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	io_conf.pull_up_en = GPIO_PULLUP_ENABLE; //add pull up during production test to check display enable signal
	gpio_config(&io_conf);

	ptr_send_que = send_que;
    return ret;
}

void dis_enb_output_set(bool level)
{
    gpio_set_level(DIS_ENB, level);
}

uint32_t dis_inputs_get(void)
{
	uint32_t pins = GPIO.in & 0xFFFF;
	pins |= (bool)(READ_PERI_REG(RTC_GPIO_IN_DATA) & 1) << 16;
	return pins;
}