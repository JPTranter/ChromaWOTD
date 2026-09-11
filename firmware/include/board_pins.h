#pragma once
// XIAO ESP32-S3 + Seeed XIAO ePaper Display Board (EE05) pin map.
// EE05 routes the 24-pin FPC to the XIAO's standard SPI pins:
//   CS=D2, DC=D3, RST=D4, BUSY=D5  (verify against the EE05 schematic in Phase 1)
// SCK/MOSI are the hardware SPI defaults on the ESP32-S3 XIAO.

#include <pins_arduino.h>

#define EPD_CS    D2
#define EPD_DC    D3
#define EPD_RST   D4
#define EPD_BUSY  D5
