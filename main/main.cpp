#include "Arduino.h"
#include <WiFi.h>

#include "rtc_wdt.h"

#define ENABLE_GxEPD2_GFX 0

#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold9pt7b.h>

#include "lvgl/lvgl.h"

#include "../env.h"
#include "http.h"

GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display(GxEPD2_290_BS(/*CS=5*/ 35, /*DC=*/38, /*RES=*/40, /*BUSY=*/36)); // DEPG0290BS 128x296, SSD1680
#define DISPLAY_POWER 33

int flushcalls = 0;

#define DISP_BUF_SIZE 128 * 296



template <typename ErrT = bool>
inline void checkImpl(ErrT val, const char* file, int line);

// Verify the success of a function that returns esp_err_t
inline void checkImpl(bool val, const char* file, int line)
{
    if (!val) {
        // URGENT TODO: STOP ROBOT

        while (true) {
            ESP_LOGE("", "Function call in %s on line %d failed", file, line);
            vTaskDelay(100);
        }
    }
}

inline void checkImpl(esp_err_t val, const char* file, int line)
{
    if (val != ESP_OK) {
        // URGENT TODO: STOP ROBOT

        while (true) {
            ESP_LOGE("", "Function call in %s on line %d failed", file, line);
            vTaskDelay(100);
        }
    }
}

#define CHECK(val) (checkImpl(val, __FILE__, __LINE__))

int pixelShift = 1;

/* Required by LVGL */
void flush_cb(lv_display_t *drv, const lv_area_t *area, uint8_t* px_map)
{
  ++flushcalls;
  printf("flush %d x:%ld y:%ld w:%ld h:%ld\n", flushcalls, area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area));

  uint8_t *buf = (uint8_t *)px_map; /*Let's say it's a 16 bit (RGB565) display*/
  int32_t x, y;
  int i = 0;
  for (y = area->y1; y <= area->y2; y++)
  {
    for (x = area->x1; x <= area->x2; x++)
    {
      //uint16_t color = 0;// (buf[i/8] |= (1u<<(i % 8))) ? GxEPD_BLACK : GxEPD_WHITE;
      //uint16_t color = (*buf & (1 << 7)) > 0 ? GxEPD_BLACK : GxEPD_WHITE;
      uint16_t color = (*buf > pixelShift) ? GxEPD_WHITE : GxEPD_BLACK;
      //uint16_t color = (rand() % 2) ? GxEPD_WHITE : GxEPD_BLACK;

      display.drawPixel((int16_t)x, (int16_t)y, color);
      //buf++;

      //CHECK(color == 0 || color == 255);

      if (*buf != 0 && *buf != 255) {
        //printf("Draw pixel %d\n", (int)*buf);
        //vTaskDelay(50);
      }
      buf++;

      i += 1;
    }
  }

  CHECK(area->x2 <= display.width() && area->y2 <= display.height());

  // Full update
  if (lv_area_get_width(area) == display.width() && lv_area_get_height(area) == display.height())
  {
    printf("Full update!");
    // display.update();
    ////display.display(true);

    display.display(false);
  }
  else
  {
    printf("Partial update!");
    // Partial update:
    // display.update(); // Uncomment to disable partial update

    // display.updateWindow(area->x1,area->y1,lv_area_get_width(area),lv_area_get_height(area));
    ////display.displayWindow(area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area));
    // delay(10);

    display.display(false);
  }

  /* IMPORTANT!!!
   * Inform the graphics library that you are ready with the flushing */
  lv_disp_flush_ready(drv);
}

/* Called for each pixel */
void set_px_cb(lv_display_t *drv, uint8_t *buf,
               lv_coord_t buf_w, lv_coord_t x, lv_coord_t y,
               lv_color_t color, lv_opa_t opa)
{
  // Test using RGB232
  int16_t epd_color = GxEPD_WHITE;

  // Color setting use: RGB232
  // Only monochrome:All what is not white, turn black
  if (color.red != 0 && color.blue != 0 && color.green != 0)
  {
    // if (color.ch.red < 7 && color.ch.green < 7 && color.ch.blue < 7) {
    epd_color = GxEPD_BLACK;
  }
  display.drawPixel((int16_t)x, (int16_t)y, epd_color);

  // If not drawing anything: Debug to see if this function is called:
  // printf("set_px %d %d R:%d G:%d B:%d\n",(int16_t)x,(int16_t)y, color.ch.red, color.ch.green, color.ch.blue);
}

#define LV_TICK_PERIOD_MS 10
static void lv_tick_task(void *arg) {
  (void) arg;
  lv_tick_inc(LV_TICK_PERIOD_MS);
}

inline void trace_impl(const char* file, int line) {
  ESP_LOGI("", "trace %d\n", line);
  //vTaskDelay(1000);
}

#define TRACE (trace_impl(__FILE__, __LINE__));

lv_display_t* disp = nullptr;
lv_obj_t *label = nullptr;
QueueHandle_t xGuiSemaphore = nullptr;

extern "C" void app_main()
{
  rtc_wdt_protect_off();    // Turns off the automatic wdt service

  for (int i = 0; i < 5; i++) {

    printf("Hello!\n");
    vTaskDelay(1000);
  }

  initArduino();
  
  WiFi.begin(SSID, PASSWORD);
  printf("\nConnecting");

  while(WiFi.status() != WL_CONNECTED){
    printf(".\n");
    delay(1000);
  }

  start_webserver();

  TRACE

  vTaskDelay(3000);

  xGuiSemaphore = xSemaphoreCreateMutex();

  SPI.begin(/*SCK*/ 39, /*MISO*/ -1, /*MOSI*/ 37, /*SS*/ -1);
  auto set = SPISettings(2000000, MSBFIRST, SPI_MODE0);

  pinMode(DISPLAY_POWER, OUTPUT);
  digitalWrite(DISPLAY_POWER, 1);

  display.init(115200, true, 50, false, SPI, set);
  display.setRotation(1);
  display.setFullWindow();
  display.firstPage();

  TRACE

  lv_init();

  //static lv_disp_draw_buf_t disp_buf;
  uint32_t size_in_px = DISP_BUF_SIZE;

  /* Initialize the working buffer depending on the selected display.
   * NOTE: buf2 == NULL when using monochrome displays. */
  //lv_disp_buf_init(&disp_buf, buf1, buf2, size_in_px);

  disp = lv_display_create(296, 128);

  TRACE

  lv_display_set_flush_cb(disp, flush_cb);

  TRACE

  #define DRAW_BUF_SIZE (DISP_BUF_SIZE * lv_color_format_get_size(lv_display_get_color_format(disp)))
  lv_color_t *draw_buf = (lv_color_t *)malloc(DRAW_BUF_SIZE);

  TRACE

  CHECK(draw_buf != NULL);

  TRACE

  lv_display_set_buffers(disp, draw_buf, NULL, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

  TRACE

  /* When using an epaper display we need to register these additional callbacks */
  //disp_drv.set_px_cb = set_px_cb;

  //disp_drv.buffer = &disp_buf;
  //lv_disp_drv_register(&disp_drv);
  lv_display_set_dpi(disp, 108);
  //lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);

  TRACE

  /* Create and start a periodic timer interrupt to call lv_tick_inc */
  const esp_timer_create_args_t periodic_timer_args = {
      .callback = &lv_tick_task,
      .name = "periodic_gui"};
  esp_timer_handle_t periodic_timer;
  CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
  CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));

  TRACE

  /* Create the demo application */
  //demo_create();

  label = lv_label_create( lv_screen_active() );
  lv_label_set_text( label, "Hello Arduino, I'm LVGL!" );
  lv_obj_align( label, LV_ALIGN_CENTER, 0, 0 );

  TRACE

  while (true)
  {
    /* Delay 1 tick (assumes FreeRTOS tick is 10ms */
    //todo: not right
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Try to take the semaphore, call lvgl related function on success */
    if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY))
    {
      lv_task_handler();
      xSemaphoreGive(xGuiSemaphore);
    }
  }
}

void op(int shift) {
  xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);

  pixelShift = shift;
  lv_obj_align( label, LV_ALIGN_CENTER, shift, 0 );

  printf("Updated shift to %d\n", shift);

  xSemaphoreGive(xGuiSemaphore);
}