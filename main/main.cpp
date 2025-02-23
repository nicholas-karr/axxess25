#include "Arduino.h"
#include <WiFi.h>

#include "rtc_wdt.h"

#include <esp_netif_sntp.h>

#include "../env.h"
#include "log.h"
#include "http.h"
#include "display.h"

#include <Adafruit_MPU6050.h>

#include <uICAL.h>

#define LV_TICK_PERIOD_MS 10
static void lv_tick_task(void *arg)
{
  (void)arg;
  lv_tick_inc(LV_TICK_PERIOD_MS);
}

lv_display_t *drv = nullptr;
QueueHandle_t xGuiSemaphore = nullptr;

std::vector<std::string> cals;

bool accelReverse = false;
int accelDim = 0;
int accelThreshold = 5;
int baseHorizAccel = -999;
int baseForwardAccel = -999;
int acted = 0;
int inactivityPeriod = 2;

int buzzerScale = 1;

int motionUpdatePeriod = 500;
int lineSpacing = 10;

int getHorizAccel(sensors_vec_t accel)
{
  return -int(accel.z);
}

int getForwardAccel(sensors_vec_t accel)
{
  return int(accel.x);
}

extern "C" void app_main()
{
  for (int i = 0; i < 3; i++)
  {
    printf("Hello!\n");
    vTaskDelay(1000);
  }

  initArduino();

  WiFi.begin(SSID, PASSWORD);
  printf("\nConnecting");

  while (WiFi.status() != WL_CONNECTED)
  {
    printf(".\n");
    delay(1000);
  }

  loadConfig();

  start_webserver();

  TwoWire wire(1);
  wire.begin(12, 11, 100000);

  Adafruit_MPU6050 mpu;
  CHECK(mpu.begin(MPU6050_I2CADDR_DEFAULT, &wire, 0));
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);

  xGuiSemaphore = xSemaphoreCreateMutex();

  SPI.begin(/*SCK*/ 39, /*MISO*/ -1, /*MOSI*/ 37, /*SS*/ -1);
  auto set = SPISettings(2000000, MSBFIRST, SPI_MODE0);

  pinMode(DISPLAY_POWER, OUTPUT);
  digitalWrite(DISPLAY_POWER, 1);

  display.init(115200, true, 50, false, SPI, set);
  display.setRotation(1);
  display.setFullWindow();
  display.firstPage();

  lv_init();

  drv = lv_display_create(296, 128);

  lv_display_set_flush_cb(drv, flush_cb);

#define DRAW_BUF_SIZE (DISP_BUF_SIZE * lv_color_format_get_size(lv_display_get_color_format(drv)))
  lv_color_t *draw_buf = (lv_color_t *)malloc(DRAW_BUF_SIZE);

  CHECK(draw_buf != NULL);

  lv_display_set_buffers(drv, draw_buf, NULL, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_display_set_dpi(drv, 108);

  /* Create and start a periodic timer interrupt to call lv_tick_inc */
  const esp_timer_create_args_t periodic_timer_args = {
      .callback = &lv_tick_task,
      .name = "periodic_gui"};
  esp_timer_handle_t periodic_timer;
  CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
  CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));

  lv_obj_t *tileView = lv_tileview_create(lv_screen_active());
  int currentRow = 0;
  int currentCol = 0;

  // Home tile
  lv_obj_t *tile0 = lv_tileview_add_tile(tileView, 0, 0, LV_DIR_HOR);
  lv_obj_t *clock = lv_label_create(tile0);
  lv_obj_align(clock, LV_ALIGN_TOP_LEFT, 0, 0);

  // Eye management bar
  lv_obj_t *bar = lv_bar_create(tile0);
  lv_obj_set_size(bar, 296 / 2, 20);
  lv_obj_align(bar, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_bar_set_range(bar, 0, 59);
  lv_bar_set_value(bar, 0, LV_ANIM_ON);

  // Calendar items
  lv_obj_t* rows[7] = {};
  for (int i = 0; i < 4; i++) {
    rows[i] = lv_label_create(tile0);
    lv_obj_set_pos(rows[i], 0, 10 + i * 10);
    char buf[50] = {};
    snprintf(buf, sizeof(buf), "Long Long Long Long Test Item %d", i);
    lv_label_set_text(rows[i], buf);
  }

  // Right 1: Image
  lv_obj_t *tile1 = lv_tileview_add_tile(tileView, 1, 0, LV_DIR_HOR);
  lv_obj_t *image = lv_image_create(tile1);
  LV_IMAGE_DECLARE(img1);
  lv_image_set_src(image, &img1);
  lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);

  // Right 2: Image
  lv_obj_t *tile2 = lv_tileview_add_tile(tileView, 2, 0, LV_DIR_HOR);
  lv_obj_t *image2 = lv_image_create(tile2);
  LV_IMAGE_DECLARE(img2);
  lv_image_set_src(image2, &img2);
  lv_obj_align(image2, LV_ALIGN_CENTER, 0, 0);

  static unsigned long lastMotionUpdate = 0;

  while (true)
  {
    vTaskDelay(100);

    /* Try to take the semaphore, call lvgl related function on success */
    if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY))
    {
      char buf[30];
      time_t now = time(NULL);
      // strftime(buf, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));
      strftime(buf, 20, "%H:%M %a, %b %d", localtime(&now));
      lv_label_set_text(clock, buf);

      lv_bar_set_value(bar, now % 60, LV_ANIM_ON);

      if (now % 60 == 0 && lv_tileview_get_tile_active(tileView) == tile0)
      {
        // Reminder flash
        display.clearScreen();
        display.display(false);
      }

      // Only do motion analysis once a second and skip it after it happens
      if (millis() - lastMotionUpdate >= motionUpdatePeriod)
      {
        lastMotionUpdate = millis();

        if (acted < inactivityPeriod)
        {
          acted += 1;
        }
        else
        {

          sensors_event_t a, g, temp;
          mpu.getEvent(&a, &g, &temp);

          /* Print out the values */
          printf("Acceleration %f %f %f\n", a.acceleration.x, a.acceleration.y, a.acceleration.z);
          printf("Rotation %f %f %f\n", g.gyro.x, g.gyro.y, g.gyro.z);
          printf("Temperature %f\n", temp.temperature);

          int val = getHorizAccel(a.acceleration);
          if (baseHorizAccel == -999)
          {
            baseHorizAccel = val;
          }
          else
          {
            if (val - baseHorizAccel > accelThreshold)
            {
              // Turn left
              printf("TURN LEFT\n");
              currentCol = (currentCol == 0) ? 0 : currentCol - 1;
              lv_tileview_set_tile_by_index(tileView, currentCol, currentRow, LV_ANIM_ON);

              acted = 0;
            }
            else if (val - baseHorizAccel < -accelThreshold)
            {
              // Turn right
              printf("TURN RIGHT\n");
              currentCol = (currentCol == 2) ? 2 : currentCol + 1;
              lv_tileview_set_tile_by_index(tileView, currentCol, currentRow, LV_ANIM_ON);

              acted = 0;
            }
          }

          val = getForwardAccel(a.acceleration);
          if (baseForwardAccel == -999)
          {
            baseForwardAccel = val;
          }
          else
          {
            if (val - baseForwardAccel > accelThreshold)
            {
              printf("Tilt forward\n");
              currentCol = (currentCol == 0) ? 0 : currentCol - 1;

              acted = 0;
            }
            else if (val - baseForwardAccel < -accelThreshold)
            {
              printf("Tilt backward\n");
              currentCol = (currentCol == 2) ? 2 : currentCol + 1;

              acted = 0;
            }
          }
        }
      }

      for (int i = 0; i < 4; i++) {
        lv_obj_set_pos(rows[i], 0, lineSpacing + i * lineSpacing);
      }

      lv_task_handler();
      xSemaphoreGive(xGuiSemaphore);
    }
  }
}

void op(int shift, const char *text)
{
  xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);

  pixelShift = shift;

  printf("Updated shift to %d\n", shift);

  xSemaphoreGive(xGuiSemaphore);
}

void loadConfig()
{
  static JsonDocument doc;
  getJsonFromPath("karrmedia.com", "/iot/cal/config.json", doc);

  setenv("TZ", doc["tz"].as<const char *>(), 1);
  tzset();

  if (doc["timeOverride"].isNull())
  {
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);
  }
  else
  {
    struct timeval tv;
    tv.tv_sec = doc["timeOverride"].as<time_t>();
    tv.tv_usec = 0;

    settimeofday(&tv, NULL);
  }

  accelReverse = doc["accelDim"].as<const char *>()[0] == '-';
  accelDim = doc["accelDim"].as<const char *>()[1] - '0';
  accelThreshold = doc["accelThreshold"].as<int>();
  baseHorizAccel = -999;

  for (std::string item : doc["cals"].as<JsonArrayConst>())
  {
    cals.push_back(item);
  }

  buzzerScale = doc["buzzerScale"].as<int>();

  motionUpdatePeriod = doc["motionUpdatePeriod"].as<int>();
  inactivityPeriod = doc["inactivityPeriod"].as<int>();
  lineSpacing = doc["lineSpacing"].as<int>();

  loadCalendar();
}

void loadCalendar() {
  char* ical = getFileFromPath("calendar.google.com", "/calendar/ical/c65422ad0b0f26bff3b482d73bc719b970fc025391957eb1e107d56a3d41a2f5%40group.calendar.google.com/private-3c9d279395cdb5cce1bd2dea7a12b900/basic.ics");

  uICAL::istream_String istm(ical);
  //uICAL::istream_stl istm(fstm);
  auto cal = uICAL::Calendar::load(istm);
  
  uICAL::DateTime begin(time(NULL));
  uICAL::DateTime end(time(NULL) + 259200);
  
  auto calIt = uICAL::new_ptr<uICAL::CalendarIter>(cal, begin, end);
  
  while(calIt->next()) {
      //std::cout << calIt->current() << std:endl;
      auto s = calIt->current().get()->as_str();
      printf("Cal Entry %s\n", s.c_str());
  }
}