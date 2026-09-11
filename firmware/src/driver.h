// ChromaClock display driver selection.
// Panel: Seeed 2.9" Quadruple Color (BWRY), 128x296, JD79661.
// Board: XIAO ePaper Display Board EE05 (XIAO ESP32-S3 Plus).
//
// Combo 512 = Setup512_Seeed_XIAO_EPaper_2inch9_BWRY (USE_BWRY_EPAPER).
// The EE05 define pulls the verified pin map from
// User_Setups/EPaper_Board_Pins_Setups.h:
//   SCLK D8, MISO none, MOSI D10, CS 44(D7), DC 10(D16),
//   BUSY 4(D3), RST 38(D11), ENABLE 43(D6)
// Setup defines (BOARD_SCREEN_COMBO=512, USE_XIAO_EPAPER_DISPLAY_BOARD_EE05)
// are passed globally via build_flags in platformio.ini so the library's
// Extensions/*.cpp compile with the same selection. Kept here for the
// Arduino-IDE build path.
# define BOARD_SCREEN_COMBO 512
# define USE_XIAO_EPAPER_DISPLAY_BOARD_EE05
