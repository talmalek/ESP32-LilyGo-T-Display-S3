#pragma once
#include <Arduino.h>

// ============================================================================
// LilyGO T-Display-S3 Hardware Pin Definitions
// ============================================================================

// Display Power & Backlight
#define PIN_POWER_ON        15  // LCD Power enable (Must be HIGH to power display)
#define PIN_LCD_BL          38  // Backlight PWM pin

// ST7789 8-Bit Parallel Bus
#define PIN_LCD_D0          39
#define PIN_LCD_D1          40
#define PIN_LCD_D2          41
#define PIN_LCD_D3          42
#define PIN_LCD_D4          45
#define PIN_LCD_D5          46
#define PIN_LCD_D6          47
#define PIN_LCD_D7          48

#define PIN_LCD_RES         5
#define PIN_LCD_CS          6
#define PIN_LCD_DC          7
#define PIN_LCD_WR          8
#define PIN_LCD_RD          9

// Physical Buttons
#define PIN_BUTTON_1        0   // Boot button (left) -> Cycle / Next App
#define PIN_BUTTON_2        14  // IO14 button (right) -> Select / Enter App

// Battery Voltage ADC
#define PIN_BAT_VOLT        4

// Screen Dimensions
#define SCREEN_WIDTH        170
#define SCREEN_HEIGHT       320
