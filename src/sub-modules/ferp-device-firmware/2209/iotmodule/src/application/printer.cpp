#include <Arduino.h>
#include <UIPEthernet.h>

#include "error.h"
#include "device_config.h"
#include "print_event.h"
#include "logger.h"
#include "que.h"

static EthernetClient client;
IPAddress localIP = {192,168,1,200};

static device_configs_t * _device_configs;
static que_t * _print_event_q;

ret_t printer_print(print_event_t * pe);

ret_t printer_init(device_configs_t * device_configs, que_t * print_event_q){

    ret_t ret = ret_Success;

    do{
        if(device_configs == nullptr){
            ret = ret_Err_Gen_NullP;
            logger.log("PRNTR: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret));
            break;
        }

        _device_configs = device_configs;

        if(print_event_q == nullptr){
            ret = ret_Err_Gen_NullP;
            logger.log("PRNTR: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret));
            break;
        }

        _print_event_q = print_event_q;

        uint8_t mac[6] = {0x00,0x01,0x02,0x03,0x04,0x05};
        Ethernet.begin(mac, localIP);

        Serial.print("localIP: ");
        Serial.println(Ethernet.localIP());
        Serial.print("subnetMask: ");
        Serial.println(Ethernet.subnetMask());
        Serial.print("gatewayIP: ");
        Serial.println(Ethernet.gatewayIP());
        Serial.print("dnsServerIP: ");
        Serial.println(Ethernet.dnsServerIP());

        Serial.println("Connecting to printer...");
		while(!client.connect(IPAddress(192,168,1,10), 9100)){
			Serial.print(".");
		}
		Serial.println("Connected to printer");

        // char cmd_initialize_printer[] = {0x1b, 0x40};
		// char cmd_unknown2[] = {0x1b, 0x52, 0x00};
		// char cmd_unknown3[] = {0x1b, 0x63, 0x33, 0x04, 0x0d};
		// char cmd_print_data[] = "This is a text...\n\r";
		// char cmd_print_and_feed[] = {0x1b, 0x4a, 0x02};
        // client.write((const uint8_t *) & cmd_initialize_printer, sizeof(cmd_initialize_printer));
		// client.write((const uint8_t *) & cmd_unknown2, sizeof(cmd_unknown2));
		// client.write((const uint8_t *) & cmd_unknown3, sizeof(cmd_unknown3));
		// client.write((const uint8_t *) & cmd_print_data, sizeof(cmd_print_data));
		// client.write((const uint8_t *) & cmd_print_data, sizeof(cmd_print_data));
		// client.write((const uint8_t *) & cmd_print_data, sizeof(cmd_print_data));
		// client.write((const uint8_t *) & cmd_print_data, sizeof(cmd_print_data));
		// client.write((const uint8_t *) & cmd_print_data, sizeof(cmd_print_data));
		// client.write((const uint8_t *) & cmd_print_and_feed, sizeof(cmd_print_and_feed));
		// delay(1000);

		// client.stop();
		// delay(1000);
		// Serial.println("Disconnected from the printer...");

        // print_event_t pe;
        // pe.fuel_type = "100";
        // pe.nozzel_id = "0";
        // pe.time_stamp = "2022-11-15T22:10:15+05:30";
        // pe.unit_price = 450;
        // pe.volume_l = 2.4;
        // pe.total_price = pe.volume_l*pe.unit_price;
        // printer_print(&pe);

    }while(false);

    return ret;
}

String get_fueltype_str(String fueltype){
    if(fueltype == "100"){
        return "Petrol 95";
    }
    return "Petrol 95";
}

void printer_keep_connected(){
    
    Ethernet.maintain();
    
    char cmd_initialize_printer[] = {0x1b, 0x40};    
    client.write((const uint8_t *) & cmd_initialize_printer, sizeof(cmd_initialize_printer));

    if(client.connected() != 1){
        Serial.println("Reonnecting to printer...");
        static unsigned long ts_connect = millis();
        while(!client.connect(IPAddress(192,168,1,10), 9100)){
            Serial.print(".");
            delay(100);

            if(millis() - ts_connect > 1000){
                Serial.println("Failed to reconnect to printer...");
                break;
            }
        }
        Serial.println("Reconnected to printer");
    }
}

unsigned long bill_number = 0;
ret_t printer_print(print_event_t * pe){

    ret_t ret = ret_Success;

    Serial.println("Printing started...");

    if(client.connected() != 1){
        Serial.println("Reonnecting to printer...");
        static unsigned long ts_connect = millis();
        while(!client.connect(IPAddress(192,168,1,10), 9100)){
            Serial.print(".");
            delay(100);

            if(millis() - ts_connect > 3000){
                ret = ret_Err_App_TimeOut;
                Serial.println("Failed to connect to printer...");
                return ret;
            }
        }
        Serial.println("Connected to printer");
    }


    // logger.log("Time : " + pe->time_stamp);
    // logger.log("Nozzel ID : " + pe->nozzel_id);
    // logger.log("Volume : " + String(pe->volume_l));
    // logger.log("Total Price : " + String(pe->total_price));
    // logger.log("Unit Price : " + String(pe->unit_price));
    // logger.log("Fuel Type : " + pe->fuel_type);

    char cmd_initialize_printer[] = {0x1b, 0x40};
    char cmd_unknown2[] = {0x1b, 0x52, 0x00};
    char cmd_unknown3[] = {0x1b, 0x63, 0x33, 0x04, 0x0d};
    // char cmd_print_data[] = "This is a text...\n\r";
    String cmd_print_data = "                                       ";
    char cmd_print_and_feed[] = {0x1b, 0x4a, 0x02};
    client.write((const uint8_t *) & cmd_initialize_printer, sizeof(cmd_initialize_printer));
    client.write((const uint8_t *) & cmd_unknown2, sizeof(cmd_unknown2));
    client.write((const uint8_t *) & cmd_unknown3, sizeof(cmd_unknown3));

    cmd_print_data = "                                       \n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "                 ARADANA                \n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "          Lanka Filling Station         \n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "          Highway Entrance Road,        \n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "              Nugaduwa, Galle           \n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "TEL: 091 223100         Fax: 091 2231101\n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "Bill Issued: " + String(pe->time_stamp) + "\n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "----------------------------------------\n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    client.write((const uint8_t *) & cmd_print_and_feed, sizeof(cmd_print_and_feed));
    delay(700);

    // cmd_print_data = "Bill Number : " + String(bill_number) + "\n\r";
    // client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "                                        \n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "Vehicle Nu: ............................\n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "                                        \n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "Dispenced Time: " + String(pe->time_stamp) + "\n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "Dispencer ID: " + String(pe->nozzel_id) + "\n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "Fuel Catogory: " + String(pe->fuel_type) + "\n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    client.write((const uint8_t *) & cmd_print_and_feed, sizeof(cmd_print_and_feed));
    delay(700);

    cmd_print_data = "                                        \n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "Dispenced Volume (l): " + String(pe->volume_l, 3) +  "\n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "Unit Price (Rs): " + String(pe->unit_price) +  "\n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "Total Price (Rs): " + String(pe->total_price) +  "\n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "                                        \n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    client.write((const uint8_t *) & cmd_print_and_feed, sizeof(cmd_print_and_feed));
    delay(700);

    cmd_print_data = "                                        \n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "........................................\n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "                Signature               \n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "           THANK YOU COME AGAIN         \n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    cmd_print_data = "           FEEDBACK 0712 209310         \n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    client.write((const uint8_t *) & cmd_print_and_feed, sizeof(cmd_print_and_feed));
    delay(700);

    cmd_print_data = "                                        \n\r";
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    client.write((const uint8_t *) & cmd_print_and_feed, sizeof(cmd_print_and_feed));
    delay(700);

    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    client.write((const uint8_t *)cmd_print_data.c_str(), cmd_print_data.length());
    client.write((const uint8_t *) & cmd_print_and_feed, sizeof(cmd_print_and_feed));
    delay(700);
    
    Serial.println("Printing complete...");
    
    // client.stop();
    // delay(1000);
    // Serial.println("Disconnected from the printer...");
    return ret;
}
