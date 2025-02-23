idf_component_register(
    SRCS "main.cpp" "img1.c" "img2.c"
    INCLUDE_DIRS "../include"
    REQUIRES GxEPD2 lvgl esp_http_server esp_http_client esp-tls Adafruit_MPU6050 uICAL
)
