idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "../include"
    REQUIRES GxEPD2 lvgl esp_http_server
)
