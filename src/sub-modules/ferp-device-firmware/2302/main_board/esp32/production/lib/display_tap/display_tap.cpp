#include "Arduino.h"
#include "display_tap.h"
#include "board_2302.h"

#define RX_BUFFER 200
#define CB_SIZE 2
#define DUMMY_ID 2

typedef struct __attribute__((packed))
{
    uint8_t sw_major;
    uint8_t sw_minor;
    uint8_t display_id;
    display_data_t display_data;
    uint32_t checksum;
} packet_data_t;

const size_t packet_data_size = sizeof(packet_data_t);
const uint32_t packet_data_lenght = sizeof(packet_data_t) - sizeof(packet_data_t::checksum); // seize of whole packet - size of checksum
const uint64_t heatbeat_timeout_ms = 30 * 1000;
const uint64_t reset_time_ms = 500;

bool reset_display_tap = false;
uint8_t dt_sw_major = 0;
uint8_t dt_sw_minor = 0;
uint32_t rx_index = 0;
uint64_t time_display_rec, time_reset;
uint8_t rx_buffer[RX_BUFFER];
display_data_t display_data;
got_fuel_event_t got_fuel_event[CB_SIZE] = {};

void init_display_tap(got_fuel_event_t evt1, got_fuel_event_t evt2)
{
    got_fuel_event[0] = evt1;
    got_fuel_event[1] = evt2;
    Serial2.setRxBufferSize(RX_BUFFER);
    Serial2.begin(115200, SERIAL_8N1);

    digitalWrite(nRESET_ESP07, HIGH);
    delay(500);
    // flushing the system
    while (Serial2.available())
    {
        Serial2.read();
    }
}
bool dt_do_ecs = false;
int dt_rxdata_index = -1;

void display_tap()
{
    uint64_t time_now = millis();

    while (Serial2.available())
    {
        uint8_t rx_byte = Serial2.read();
        if (dt_rxdata_index == -1)
        {
            if (rx_byte == 0xFF)
            {
                dt_rxdata_index = 0;
            }
            continue;
        }
        else if (rx_byte == 0xFF)
        {
            dt_do_ecs = false;
            if (dt_rxdata_index > 4) // got the packet
            {
                packet_data_t rx_data = {};
                uint32_t checksum = 0;
                // rx_packet_len = dt_rxdata_index;
                dt_rxdata_index = -1;

                memcpy((void *)&rx_data, rx_buffer, sizeof(packet_data_t));
                for (size_t i = 0; i < packet_data_lenght; i++)
                {
                    checksum += ((uint8_t *)&rx_data)[i];
                }
                if (checksum == rx_data.checksum)
                {
                    time_display_rec = time_now;
                    if (rx_data.display_id < CB_SIZE)
                    {
                        dt_sw_major = rx_data.sw_major;
                        dt_sw_minor = rx_data.sw_minor;
                        if (got_fuel_event[rx_data.display_id])
                            got_fuel_event[rx_data.display_id](rx_data.display_data);
                    }
                    // else if (rx_data.display_id == DUMMY_ID)
                    // {
                    //     Serial.println("Got dummy packet");
                    // }
                    else if(rx_data.display_id > DUMMY_ID)
                    {
                        Serial.println("Invalid ID:" + String(rx_data.display_id));
                    }
                }
                else
                {
                    Serial.println("Serial data error ");
                }
            }
            else
            {
                dt_rxdata_index = 0; // otherwise not enough data, dump it
            }
            continue;
        }

        // escaped char marker
        if (rx_byte == 0xFE)
        {
            // yes, so flag the next char must be unescaped
            dt_do_ecs = true;
            continue;
        }

        // need to unescape?
        if (dt_do_ecs)
        {
            rx_byte = ~rx_byte;
            dt_do_ecs = false;
        }

        // index within range?
        if (dt_rxdata_index < RX_BUFFER)
        {
            rx_buffer[dt_rxdata_index] = rx_byte;
            dt_rxdata_index++;
        }
        else
        {
            // flag too many bytes received, revert to awaiting SOM
            dt_rxdata_index = -1;
        }
    }

    // Serial.println();
    if ((time_now - time_display_rec) > heatbeat_timeout_ms)
    {
        time_display_rec = time_now;
        Serial.println("Display Tap Offline. Resetting...");
        digitalWrite(nRESET_ESP07, LOW); // starting reset
        reset_display_tap = true;
        time_reset = time_now;
    }

    // Holding reset pin down without blocking the while
    if (reset_display_tap)
    {
        if ((time_now - time_reset) > reset_time_ms)
        {
            reset_display_tap = false;
            digitalWrite(nRESET_ESP07, HIGH); // reset done
            Serial.println("DT reset done...");
        }
    }
}

inline uint8_t get_dt_sw_minor()
{
    return dt_sw_minor;
}

inline uint8_t get_dt_sw_major()
{
    return dt_sw_major;
}