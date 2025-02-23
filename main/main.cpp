#include "Arduino.h"
#include <WiFi.h>

#include "rtc_wdt.h"

#include <esp_netif_sntp.h>

#include "../env.h"
#include "log.h"
#include "http.h"
#include "display.h"

#include <Adafruit_MPU6050.h>

#define LV_TICK_PERIOD_MS 10
static void lv_tick_task(void *arg)
{
  (void)arg;
  lv_tick_inc(LV_TICK_PERIOD_MS);
}

lv_display_t *drv = nullptr;
lv_obj_t *label = nullptr;
QueueHandle_t xGuiSemaphore = nullptr;

#define BUZZER_PIN 5

std::vector<std::string> cals;

bool accelReverse = false;
int accelDim = 0;
int accelThreshold = 5;
int baseAccel = -999;

int buzzerScale = 1;

int getAccel(sensors_vec_t accel) {
  float val = 0;

  if (accelDim == 0) {
    val = accel.x;
  }
  else if (accelDim == 1) {
    val = accel.y;
  }
  else {
    val = accel.z;
  }

  return int(val) * (accelReverse ? -1 : 1);
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

  pinMode(BUZZER_PIN, OUTPUT);
  analogWrite(BUZZER_PIN, pixelShift);

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
  label = lv_label_create(tile0);
  lv_label_set_text(label, "Hello Arduino, I'm LVGL!");
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);

  // Right 1: Image
  lv_obj_t *tile1 = lv_tileview_add_tile(tileView, 1, 0, LV_DIR_HOR);
  lv_obj_t* image = lv_image_create(tile1);
  LV_IMAGE_DECLARE(img1);
  lv_image_set_src(image, &img1);
  lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);

  while (true)
  {
    vTaskDelay(1000);

    /* Try to take the semaphore, call lvgl related function on success */
    if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY))
    {
      char buf[30];
      time_t now = time(NULL);
      strftime(buf, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));
      lv_label_set_text(label, buf);

      sensors_event_t a, g, temp;
      mpu.getEvent(&a, &g, &temp);

      /* Print out the values */
      printf("Acceleration %f %f %f\n", a.acceleration.x, a.acceleration.y, a.acceleration.z);
      printf("Rotation %f %f %f\n", g.gyro.x, g.gyro.y, g.gyro.z);
      printf("Temperature %f\n", temp.temperature);

      int val = getAccel(a.acceleration);
      if (baseAccel == -999) {
        baseAccel = val;
      }
      else {
        if (val - baseAccel > accelThreshold) {
          // Turn left
          printf("TURN LEFT\n");
          currentCol -= 1;
          lv_tileview_set_tile_by_index(tileView, currentCol, currentRow, LV_ANIM_ON);
        }
        else if (val - baseAccel < -accelThreshold) {
          // Turn right
          printf("TURN RIGHT\n");
          currentCol += 1;
          lv_tileview_set_tile_by_index(tileView, currentCol, currentRow, LV_ANIM_ON);
        }
      }

      snprintf(buf, sizeof(buf), "Acc %f", a.acceleration.x);
      lv_label_set_text(label, buf);

      lv_task_handler();
      xSemaphoreGive(xGuiSemaphore);
    }
  }
}

void op(int shift, const char *text)
{
  xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);

  pixelShift = shift;
  analogWrite(BUZZER_PIN, pixelShift);
  // lv_obj_align(label, LV_ALIGN_CENTER, shift, 0);
  // lv_label_set_text(label, text);

  printf("Updated shift to %d\n", shift);

  xSemaphoreGive(xGuiSemaphore);
}

void loadConfig()
{
    static JsonDocument doc;
    getJsonFromPath("karrmedia.com", "/iot/cal/config.json", doc);

    setenv("TZ", doc["tz"].as<const char*>(), 1);
    tzset();

    if (doc["timeOverride"].isNull()) {
      esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
      esp_netif_sntp_init(&config);
    }
    else {
      struct timeval tv;
      tv.tv_sec = doc["timeOverride"].as<time_t>();
      tv.tv_usec = 0;

      settimeofday(&tv,NULL);
    }

    accelReverse = doc["accelDim"].as<const char*>()[0] == '-';
    accelDim = doc["accelDim"].as<const char*>()[1] - '0';
    accelThreshold = doc["accelThreshold"].as<int>();
    baseAccel = -999;

    for (std::string item : doc["cals"].as<JsonArrayConst>()) {
      cals.push_back(item);
    }

    buzzerScale = doc["buzzerScale"].as<int>();
}