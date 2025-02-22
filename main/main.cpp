#include "Arduino.h"

#define ENABLE_GxEPD2_GFX 0

#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold9pt7b.h>

GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display(GxEPD2_290_BS(/*CS=5*/ 35, /*DC=*/ 38, /*RES=*/ 40, /*BUSY=*/ 36)); // DEPG0290BS 128x296, SSD1680
#define DISPLAY_POWER 33

extern "C" void app_main()
{
  initArduino();

  SPI.begin(/*SCK*/ 39, /*MISO*/ -1, /*MOSI*/ 37, /*SS*/ -1);
  auto set = SPISettings(2000000, MSBFIRST, SPI_MODE0);

  pinMode(DISPLAY_POWER, OUTPUT);
  digitalWrite(DISPLAY_POWER, 1);

  display.init(115200, true, 50, false, SPI, set);

  while(true){
    printf("loop\n");
    delay(1000);
  }
  
  while (true);
}