#ifndef LED_UI_H
#define LED_UI_H

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wpa2.h"
#include "esp_smartconfig.h"
#include <time.h>
#include <sys/time.h>
#include "esp_attr.h"
#include "esp_sntp.h"
#include <sys/param.h>
#include "driver/rmt.h"
#include "led_strip.h"

void led_strip_remap();
void color_init();
void num_display(led_strip_t *strip, uint8_t col, uint8_t num);
void centi_display(led_strip_t *strip, uint8_t col);
void weather_refresh(led_strip_t *strip, uint8_t *weather_info, int8_t set);
void time_refresh(led_strip_t *strip, char *time, int8_t set);
void frequency_spectrum_refresh(led_strip_t *strip, uint8_t *led_dft);
void data_refresh(led_strip_t *strip, char *time, int8_t set);

#endif