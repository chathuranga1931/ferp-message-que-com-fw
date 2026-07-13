#include <stdio.h>
#include "driver/gpio.h"
#include "sys_types.h"
#include "board_io.h"
#include "production_io.h"

esp_err_t production_io_init()
{
    esp_err_t ret = ESP_OK;
	gpio_config_t io_conf;

	// set display enable pin output and turn off. set chip select signals out
	io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pin_bit_mask = BIT64(DIS_ENB) | BIT64(D1_OUT_CS) | BIT64(D2_OUT_CS) | BIT64(DIS_LED);
	io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
	ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));
    
    //set inputs
    io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = BIT64(D1_SCLK) | BIT64(D1_RCLK) | BIT64(D1_SDATA1) | BIT64(D1_SDATA2) | BIT64(D2_SCLK) | BIT64(D2_RCLK) | BIT64(D2_SDATA1) | BIT64(D2_SDATA2) | BIT64(D1_IN_CS) | BIT64(D2_IN_CS);
	io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	io_conf.pull_up_en = GPIO_PULLUP_ENABLE; //add pull up during production test to check display enable signal
	ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

end:
    return ret;
}

void dis_enb_output_set(bool level)
{
    gpio_set_level(DIS_ENB, level);
}

void dis_led_output_set(bool level)
{
    gpio_set_level(DIS_LED, level);
}

void dis_out_cs1_set(bool level)
{
	gpio_set_level(D1_OUT_CS, level);
}
void dis_out_cs2_set(bool level)
{
	gpio_set_level(D2_OUT_CS, level);
}

uint32_t dis_inputs_get(void)
{
	input_pin_t input_pins = {0};
	
	input_pins.d1_sclk = gpio_get_level(D1_SCLK);
	input_pins.d1_rclk = gpio_get_level(D1_RCLK);
	input_pins.d1_sdata1 = gpio_get_level(D1_SDATA1);
	input_pins.d1_sdata2 = gpio_get_level(D1_SDATA2);

	input_pins.d2_sclk = gpio_get_level(D2_SCLK);
	input_pins.d2_rclk = gpio_get_level(D2_RCLK);
	input_pins.d2_sdata1 = gpio_get_level(D2_SDATA1);
	input_pins.d2_sdata2 = gpio_get_level(D2_SDATA2);

	input_pins.d1_in_cs = gpio_get_level(D1_IN_CS);
	input_pins.d2_in_cs = gpio_get_level(D2_IN_CS);

	return input_pins.u32int;
}