idf_component_register(
	SRCS "main.c"
	INCLUDE_DIRS "."
	REQUIRES led_panel DS3231 DS18B20 esp_driver_uart
)