idf_component_register(
	SRCS "led_panel.c"
	INCLUDE_DIRS "."
	REQUIRES esp_driver_gpio esp_driver_ledc esp_timer nvs_flash esp_driver_uart
)