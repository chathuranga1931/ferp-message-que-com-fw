
/* GPIO Configs */
#define GPIO_BTN_DEFAULT    (36)

#define GPIO_LED_RED        (27)
#define GPIO_LED_GREEN1     (14)
#define GPIO_LED_GREEN2     (12)

#define SPI_CS_SD           (15)

#define RXD2                (16)
#define TXD2                (17)

#define I2C_SDA             (21)
#define I2C_SCL             (22)

#define GPIO_N_RESET_ESP07  (2)


// #ifndef BOARD_2302_H_
// #define BOARD_2302_H_


// // INPUTS
// #define SWITCH          36
// #define VIN_LOW         39
// #define INPUT1          34
// #define INPUT2          35
// #define IRQ_RF          4


// // OUTPUTS
// #define nRESET_ESP32    32
// #define BUZZER          33
// #define OUTPUT1         26
// #define OUTPUT2         25
// #define LED1            27
// #define LED2            14
// #define LED3            12
// #define EN_4G           13
// #define nRESET_ESP07    2
// #define SPI_CS_SD       15
// #define SPI_CS_RF       5
// #define RESET_RF        0

// // SPI
// #define SPI_MOSI        23
// #define SPI_MISO        19
// #define SPI_SCLK        18

// // I2C
// #define I2C_SCL         22
// #define I2C_SDA         21


// #ifdef __cplusplus
// extern "C" {
// #endif

// void initBoard();

// void attach_pwrdwn_event(void(*event)(void));

// #ifdef __cplusplus
// }
// #endif

// #endif //BOARD_2302_H_