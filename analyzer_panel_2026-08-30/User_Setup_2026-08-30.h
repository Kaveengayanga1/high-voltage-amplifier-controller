// User_Setup.h — TFT_eSPI config for STM32F411 Black Pill + ILI9488 (8-bit parallel)
// FINAL pin map (rev 3): data bus PB0-PB7, control PB9/PB10/PB12/PB13/PB14
// ADC: PA0 signal, PA1 thermistor. PA2-PA5 gain control, PA6 fan PWM.
// COPY THIS ENTIRE FILE over Documents\Arduino\libraries\TFT_eSPI\User_Setup.h

#define STM32
#define TFT_PARALLEL_8_BIT
#define STM_PORTB_DATA_BUS   // data on PB0-PB7 -> ~8x faster writes

#define ILI9488_DRIVER

#define TFT_WIDTH  320
#define TFT_HEIGHT 480

// Control pins
#define TFT_RST   PB12
#define TFT_CS    PB9
#define TFT_DC    PB10   // LCD_RS on the shield
#define TFT_WR    PB13
#define TFT_RD    PB14

// Data pins D0..D7
#define TFT_D0    PB0
#define TFT_D1    PB1
#define TFT_D2    PB2    // BOOT1 pin: has on-board pull-down, OK after reset
#define TFT_D3    PB3
#define TFT_D4    PB4
#define TFT_D5    PB5
#define TFT_D6    PB6
#define TFT_D7    PB7

// Fonts
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
