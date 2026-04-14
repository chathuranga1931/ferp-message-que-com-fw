#ifndef DISPLAY_TAP_H_
#define DISPLAY_TAP_H_


#ifdef __cplusplus
extern "C" {
#endif

typedef union
{
	struct
	{
		uint8_t LS : 4; // first 4 bits
		uint8_t MS : 4; // last 4 bits
	};
	uint8_t u8int;
} tuByte_t;

typedef union __attribute__((packed))
{
	struct
	{
		uint8_t start_stop : 1;
		uint8_t select_p : 1;
		uint8_t select_l : 1;
		uint8_t rest : 5;
	};
	uint8_t byte;
} flags_t;

typedef struct __attribute__((packed))
{
	flags_t flags;
	uint32_t unit_price;  // 1 * 0.01 price
	uint32_t total_price; // 1 * 0.01 price
	uint32_t volume_l;	  // 1 * 0.001 volume
} display_data_v2_t;

typedef void (*got_fuel_event_t)(display_data_v2_t data);

void init_display_tap(got_fuel_event_t evt1, got_fuel_event_t evt2);
void display_tap();
uint8_t get_dt_sw_minor();
uint8_t get_dt_sw_major();

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_TAP_H_ */