#include <freertos/FreeRTOS.h> // Mandatory first include

#include <esp32-hal-log.h>

template <typename ErrT = bool>
inline void checkImpl(ErrT val, const char *file, int line);

// Verify the success of a function that returns esp_err_t
inline void checkImpl(bool val, const char *file, int line)
{
  if (!val)
  {
    // URGENT TODO: STOP ROBOT

    while (true)
    {
      ESP_LOGE("", "Function call in %s on line %d failed", file, line);
      vTaskDelay(100);
    }
  }
}

inline void checkImpl(esp_err_t val, const char *file, int line)
{
  if (val != ESP_OK)
  {
    // URGENT TODO: STOP ROBOT

    while (true)
    {
      ESP_LOGE("", "Function call in %s on line %d failed", file, line);
      vTaskDelay(100);
    }
  }
}

#define CHECK(val) (checkImpl(val, __FILE__, __LINE__))