/*
 * File:   main.c
 * Author: Chathuranga
 *
 * Created on October 2, 2022, 6:40 PM
 */

// CONFIG
#pragma config FOSC = HS        // Oscillator Selection bits (HS oscillator)
#pragma config WDTE = OFF       // Watchdog Timer Enable bit (WDT disabled)
#pragma config PWRTE = OFF       // Power-up Timer Enable bit (PWRT enabled)
#pragma config BOREN = OFF      // Brown-out Reset Enable bit (BOR disabled)
#pragma config LVP = OFF         // Low-Voltage (Single-Supply) In-Circuit Serial Programming Enable bit (RB3/PGM pin has PGM function; low-voltage programming enabled)
#pragma config CPD = OFF        // Data EEPROM Memory Code Protection bit (Data EEPROM code protection off)
#pragma config WRT = OFF        // Flash Program Memory Write Enable bits (Write protection off; all program memory may be written to by EECON control)
#pragma config CP = OFF         // Flash Program Memory Code Protection bit (Code protection off)

// #pragma config statements should precede project file includes.
// Use project enums instead of #define for ON and OFF.

#include <xc.h>		 
#define uchar unsigned char
#define uint unsigned int

void Board_init(void)
{
    TRISA=0xFF;	   
	TRISB=0xFF;
	TRISC=0xFF;
	TRISD=0xFF;
    TRISE=0XFF;      
}

#define _XTAL_FREQ (8000000)
 
#define PORT_LED_1      PORTCbits.RC0
#define PORT_LED_2      PORTCbits.RC1
#define PORT_LED_4      PORTAbits.RA5
#define PORT_LED_3      PORTEbits.RE0

#define PORT_SW_IN1     PORTCbits.RC3
#define PORT_SW_IN2     PORTCbits.RC2
#define PORT_SW_IN3     PORTEbits.RE1
#define PORT_SW_IN4     PORTEbits.RE2

#define TRIS_LED_1      TRISCbits.TRISC0
#define TRIS_LED_2      TRISCbits.TRISC1
#define TRIS_LED_4      TRISAbits.TRISA5
#define TRIS_LED_3      TRISEbits.TRISE0

#define TRIS_SW_IN1     TRISCbits.TRISC3
#define TRIS_SW_IN2     TRISCbits.TRISC2
#define TRIS_SW_IN3     TRISEbits.TRISE1
#define TRIS_SW_IN4     TRISEbits.TRISE2

#define STATUS_LED_DELAY_MS                     (1000)
#define DELAY_DATA_SENDING_MS                    (250)

#define SW_INPUT_REPEAT_COUNT   (5)
        
volatile unsigned int counter_10ms = 0;
volatile unsigned int ts_rclk_hit_ticks = 0;
volatile unsigned int ts_status_led = 0;
volatile unsigned char data_ready_send = 0;
unsigned int ts_data_sending = 0;

volatile unsigned char txbuffer[36];
volatile unsigned char txbuffer_completed[36];
volatile unsigned char txbuffer_intermediate[36];
volatile unsigned char txbuffer_corrected[34];
volatile unsigned char sw_input_status = 0;
volatile unsigned char prev_sw_input_status = 0;
unsigned int blink_delay_10ms = 100;

unsigned char sw_input_repeated = 0;
unsigned char sw_input_status_prev = 0;

void loop();

void main(void) {
    
    Board_init(); 
    
    TRISB = 0xFF;
    TRISD = 0xFF;
    TRISCbits.TRISC4 = 1;
    
    /* Uart */
    TRISC = 0XFF;
    SPBRG = 0X33;   /* Fosc = 8MHz, BR = 9600 */
    TXSTA = 0X24;
    RCSTA = 0X90;
    
    /* External Interrupt */
    TRISBbits.TRISB0 = 1;     
    INTE  = 1;
    INTEDG = 0; /* Falling edge interrupt */
    
    /* PORT E/A Special Configuration */
    TRISEbits.PSPMODE = 0;
    ADCON1bits.PCFG = 0b0110; /* All analog pins configured as Digital IOs */
    
    /* LEDs */
    TRIS_LED_1 = 0;      
    TRIS_LED_2 = 0;     
    TRIS_LED_3 = 0;      
    TRIS_LED_4 = 0;      

    /* Switches */
    TRIS_SW_IN1 = 1;     
    TRIS_SW_IN2 = 1;      
    TRIS_SW_IN3 = 1;     
    TRIS_SW_IN4 = 1;      

    TRISAbits.TRISA2 = 1;
    TRISAbits.TRISA3 = 1;
    
    T1OSCEN = 0x0;
    T1CONbits.T1CKPS = 0b11; /* 1:8 */
    TMR1CS = 0x0; /* Fosc/4 */
    // 25 counts, of (8Mhz/4)/8 => 250kHz => 4uS => 4uS * 2500 => 10 mS intervals
    // 0xFFFF - 0x09C4 = 0xF63B
    
    TMR1H = 0xF6;
    TMR1L = 0x3B; 
    TMR1ON = 0x1;
    TMR1IE = 0x1;
    
    RCIE  = 0x1;
    GIE   = 0x1;
    PEIE  = 0x1;  
    
    loop();
}

void loop(){        
    
    PORT_LED_1 = 0;
    PORT_LED_2 = 0;
    PORT_LED_3 = 0;
    PORT_LED_4 = 0;
    
    int idx = 0;
    txbuffer_corrected[idx++] = 's';
    txbuffer_corrected[idx++] = 't';
    txbuffer_corrected[idx++] = 'x';
    txbuffer_corrected[idx++] = '0';
    txbuffer_corrected[idx++] = '2';    //version (00 to ZZ) 
    txbuffer_corrected[idx++] = sw_input_status;
    
    for(int i=6; i<36; i++){
        txbuffer_corrected[i] = 0xFF;
        txbuffer_completed[i] = 0xFF;
        txbuffer_intermediate[i] = 0xFF;
    }
    
    blink_delay_10ms = 20;
    while(1){
        
        PORT_LED_1 = (~PORT_SW_IN3) | (~PORT_SW_IN4);
        PORT_LED_2 = (~PORT_SW_IN1) | (~PORT_SW_IN2);  
        
        if(counter_10ms - ts_status_led > blink_delay_10ms){
            PORT_LED_3 = ~PORT_LED_3;
            ts_status_led = counter_10ms;
        }
        
        if(data_ready_send == 1){
            for(int i=6; i<36; i++){
                txbuffer_completed[i] = txbuffer[i];
            }
        }
        
        if(counter_10ms - ts_data_sending > DELAY_DATA_SENDING_MS/10){
            
            ts_data_sending = counter_10ms;
            
            txbuffer_corrected[5] = sw_input_status;
            if(sw_input_status_prev != sw_input_status){
                sw_input_status_prev = sw_input_status;
                sw_input_repeated = 0;
            }
            
            if(sw_input_repeated >= SW_INPUT_REPEAT_COUNT){                
                sw_input_status = 0;
            }
            
            sw_input_repeated++;            
            
            for(int i=6; i<36; i++){
                unsigned char tmp_var = txbuffer_completed[i];
                unsigned char tmp_var_msb = 0;
                for(int j=0; j<8; j++){
                    tmp_var_msb = tmp_var_msb << 1;
                    tmp_var_msb |= tmp_var & 0x01;
                    tmp_var = tmp_var >> 1;                        
                }
                txbuffer_intermediate[i] = tmp_var_msb;
            }
            txbuffer_intermediate[20] = 0xFF;
            txbuffer_intermediate[21] = 0xFF;   /* 0x7th element will not be filled with 
                                                 * actual data because there is only 14 
                                                 * elements 0 to 13, so the 7th element 
                                                 * is at rotated 14 which will be 0 always.
                                                 * That converts to 0 at 0th and 1st element. 
                                                 * which will override 0th and 1st element 
                                                 * actual data. */ 
            for(int i=6; i<36; i+=2){
                unsigned char tmp_var1 = txbuffer_intermediate[i];
                unsigned char tmp_var2 = txbuffer_intermediate[i+1];
                unsigned char idx_correct = 6 + ((tmp_var1 & 0x0F)*2);
                txbuffer_corrected[idx_correct] = tmp_var1;
                txbuffer_corrected[idx_correct + 1] = tmp_var2;
            }
            
            if(PORT_SW_IN2 == 0){            
                for(int i=0; i<34; i++){
                    TXREG = txbuffer_corrected[i];
                    asm ("NOP"); 
                    asm ("NOP"); 
                    while(!TXIF);
                } 
            }
        }      
    }
} 

void __interrupt() interrupt_isr(void)
{
    GIE = 0;
    if(RCIE && RCIF) 
    {
        if(RCREG == 0x30)
        RE1 = 1; 
        else if(RCREG == 0X31)
        RE1 = 0; 
    }

    if(TMR1IE && TMR1IF){
        TMR1IF = 0;
        TMR1H = 0xF6;
        TMR1L = 0x3B; 
        counter_10ms++;
        
        /* every 10 ms, read the switch input value */
        sw_input_status = sw_input_status | (((~PORT_SW_IN1) & 0x01) << 0);
        sw_input_status = sw_input_status | (((~PORT_SW_IN2) & 0x01) << 1);
        sw_input_status = sw_input_status | (((~PORT_SW_IN3) & 0x01) << 2);
        sw_input_status = sw_input_status | (((~PORT_SW_IN4) & 0x01) << 3);
    }
    
    if(INTE && INTF)  
    {
        INTF = 0;            
            
        volatile static unsigned char tmp;        
        volatile static unsigned char idx;
        
        tmp = (PORTB & 0b00111110) | (PORTCbits.RC4) | (PORTAbits.RA2 << 7) | (PORTAbits.RA3 << 6);
        idx = tmp & 0xf0;
        idx = idx >> 4;
        txbuffer[6 + 2*idx ] = tmp;   

        tmp = (PORTD);
        idx = tmp & 0xf0;
        idx = idx >> 4;
        txbuffer[6 + 2*idx + 1 ] = tmp; 
        
        if(idx == 11){ /* 0xD's rotated value */
            data_ready_send = 1;            
        }
        else{            
            data_ready_send = 0;  
        }  
    }
    GIE = 1;
}