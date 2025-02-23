#define ENABLE_GxEPD2_GFX 0

#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>

#include "lvgl/lvgl.h"

GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display(GxEPD2_290_BS(/*CS=5*/ 35, /*DC=*/38, /*RES=*/40, /*BUSY=*/36)); // DEPG0290BS 128x296, SSD1680
#define DISPLAY_POWER 33


#define DISP_BUF_SIZE 128 * 296

int pixelShift = 1;

void flush_cb(lv_display_t *drv, const lv_area_t *area, uint8_t *px_map)
{
  uint8_t *buf = (uint8_t *)px_map; /*Let's say it's a 16 bit (RGB565) display*/
  for (int y = area->y1; y <= area->y2; y++)
  {
    for (int x = area->x1; x <= area->x2; x++)
    {
      uint16_t color = (*buf > 127) ? GxEPD_WHITE : GxEPD_BLACK;
      
      display.drawPixel((int16_t)x, (int16_t)y, color);

      buf++;
    }
  }

  if (lv_area_get_width(area) == display.width() && lv_area_get_height(area) == display.height())
  {
    printf("Full update!");
    display.display(true);
  }
  else
  {
    printf("Partial update!");
    display.displayWindow(area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area));
  }

  // Let LVGL know that flushing is done
  lv_disp_flush_ready(drv);
}