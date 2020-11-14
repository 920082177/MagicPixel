/* Play music from Bluetooth device

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
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
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "i2s_stream.h"
#include "esp_peripherals.h"
#include "periph_touch.h"
#include "periph_adc_button.h"
#include "periph_button.h"
#include "board.h"
#include "filter_resample.h"
#include "audio_mem.h"
#include "bluetooth_service.h"
#include "driver/rmt.h"
#include "soc/soc.h"
#include "math.h"
#include "mqtt_client.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include <lwip/netdb.h>

#include "led_strip.h"
#include "led_ui.h"
#include "DFT.h"

#define EXAMPLE_ESP_WIFI_SSID      "bzmc"
#define EXAMPLE_ESP_WIFI_PASS      "02365833961"
#define EXAMPLE_ESP_MAXIMUM_RETRY  1

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT2;


static const char *TAG = "BLUETOOTH_EXAMPLE";
static const char *TAG_CAR = "little car";
static int s_retry_num = 0;

QueueHandle_t xQueue_time;
QueueHandle_t xQueue_PCM;
QueueHandle_t xQueue_led_dft;
QueueHandle_t xQueue_is_audio;//判断是否处于音频模式
QueueHandle_t xQueue_weather;
QueueHandle_t xQueue_page;  //当前页数

TaskHandle_t xHandle_sntp;
TaskHandle_t xHandle_tcp;

static void smartconfig_example_task(void *parm);
static void sntp_get_time_task(void *parm);
static void tcp_client_task(void *parm);
static void led_strip_task(void *parm);
static void bt_mp3_task(void *parm);
static void DFT_task(void *parm);

static void wifi_init_sta(void);
static void initialize_sntp(void);

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {          //smartconfig部分
        ESP_LOGI(TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);

        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
        ESP_ERROR_CHECK( esp_wifi_connect() );
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
            ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            char data_str[2] = {0};
            data_str[0] = *(event->data);
            data_str[1] = '\0';
            int8_t action = -1;
            printf("%s", data_str);
            if(strcmp(data_str, "l") == 0 || strcmp(data_str, "L") == 0)
            {
                printf("commond is left");
                action = -1;
                xQueueSendToBack(xQueue_page, &action, 0);
            }
            else if(strcmp(data_str, "r") == 0 || strcmp(data_str, "R") == 0)
            {
                printf("commond is right");
                action = 1;
                xQueueSendToBack(xQueue_page, &action, 0);
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

static void smartconfig_example_task(void * parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY); 
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}

static void sntp_get_time_task(void *parm)
{
    time_t now = 0;
    struct tm timeinfo = { 0 };
    char strftime_buf[64];
    
    while(1)
    {
        //ESP_LOGI(TAG_CAR, "sntp task start.");
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "The current date/time in Shanghai is: %s", strftime_buf);
        xQueueSendToBack(xQueue_time, strftime_buf, 0);
        ESP_LOGI(TAG_CAR, "sntp task send time to queue_time");
        //ESP_LOGI(TAG_CAR, "sntp task end.");
        vTaskDelay(1000/portTICK_RATE_MS);
    }
    vTaskDelete(NULL);
}

static void tcp_client_task(void *parm)
{
    char rx_buffer[1500];
    char addr_str[128];
    int addr_family;
    int ip_protocol;
    const char* xinzhi_ip_addr = "116.62.81.138";
    uint16_t xinzhi_port = 80;
    //const char *payload = "GET https://api.seniverse.com/v3/weather/now.json?key=SA5D4bauEyPakeoU0&location=chongqing&language=zh-Hans&unit=c HTTP/1.1\r\nHost: api.seniverse.com\r\n\r\n";
    const char *payload = "GET https://api.seniverse.com/v3/weather/daily.json?key=SA5D4bauEyPakeoU0&location=chongqing&language=zh-Hans&unit=c&start=0&days=5 HTTP/1.1\r\nHost: api.seniverse.com\r\n\r\n";
    while(1)
    {
        //使用ipv4
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(xinzhi_ip_addr);    //将要访问的IP地址写入socketaddr
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(xinzhi_port);                       //设置访问端口
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

        int sock =  socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created, connecting to %s:%d", xinzhi_ip_addr, xinzhi_port);
        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0) {
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Successfully connected");

        while (1) {
            int err = send(sock, payload, strlen(payload), 0);
            if (err < 0) {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                break;
            }

            int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            // Error occurred during receiving
            if (len < 0) {
                ESP_LOGE(TAG, "recv failed: errno %d", errno);
                break;
            }
            // Data received
            else {
                rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
                ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
                ESP_LOGI(TAG, "data is %s", rx_buffer);
                char *p = rx_buffer;
	            uint8_t weather_code = 99, temperature = 0;
                uint8_t weather_info[3][3] = {0};
                for(int i = 0; i < 3; i++)
                {
                    p = strstr(p, "code_day");  //今天的天气
                    if( *(p+12)>='0' && *(p+12)<='9' )//判断是不是两位
                        weather_info[i][0] = 10*(*(p+11)-48) + (*(p+12)-48);
                    else
                        weather_info[i][0] = *(p+11)-48;
                    p = strstr(p, "high");              //今天的最高气温
                    if( *(p+8)>='0' && *(p+8)<='9' )//判断是不是两位
                        weather_info[i][1] = 10*(*(p+7)-48) + (*(p+8)-48);
                    else
                        weather_info[i][1] = *(p+7)-48;
                    p = strstr(p, "low");               //今天的最低气温
                    if( *(p+7)>='0' && *(p+7)<='9' )//判断是不是两位
                        weather_info[i][2] = 10*(*(p+6)-48) + (*(p+7)-48);
                    else
                        weather_info[i][2] = *(p+6)-48;

                    printf("day %d: weather code is %d, high is %d, low is %d", i, weather_info[i][0], weather_info[i][1], weather_info[i][2]);
                }
                xQueueSendToBack(xQueue_weather, &weather_info[0][0], 0);
                /*
                p = strstr(rx_buffer, "code");
                if( *(p+8)>='0' && *(p+8)<='9' )//判断是不是两位
                {
                    weather_code = 10*(*(p+7)-48) + (*(p+8)-48);
                }
                else
                {
                    weather_code = *(p+7)-48;
                }
                p = strstr(rx_buffer, "temperature");
                if( *(p+15)>='0' && *(p+15)<='9' )
                {
                    temperature = 10*(*(p+14)-48) + (*(p+15)-48); //这里必须用++p
                }
                else
                {
                    temperature = *(p+14)-48;
                }
                weather_info[0] = weather_code;
                weather_info[1] = temperature;
                //printf("weather code is %d, temp is %d", weather_code, temperature);
                xQueueSendToBack(xQueue_weather, &weather_info[0], 0);
                */
            }

            vTaskDelay(10000 / portTICK_PERIOD_MS);
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

static void led_strip_task(void *parm)
{
    //LED strip初始化
    uint32_t red = 0;
    uint32_t green = 0;
    uint32_t blue = 0;
    uint16_t hue = 0;
    //uint16_t start_rgb = 0;

    led_strip_remap();
    color_init();
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(CONFIG_EXAMPLE_RMT_TX_GPIO, RMT_TX_CHANNEL);
    // set counter clock to 40MHz
    config.clk_div = 2;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    // install ws2812 driver
    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(CONFIG_EXAMPLE_STRIP_LED_NUMBER, (led_strip_dev_t)config.channel);
    led_strip_t *strip = led_strip_new_rmt_ws2812(&strip_config);
    if (!strip) {
        ESP_LOGE(TAG, "install WS2812 driver failed");
    }
    // Clear LED strip (turn off all LEDs)
    ESP_ERROR_CHECK(strip->clear(strip, 100));
    // Show simple rainbow chasing pattern
    //ESP_LOGI(TAG, "LED Rainbow Chase Start");
    char Rec_time[64] = {0};
    uint8_t led_dft_num[32] = {0};
    uint8_t is_audio = 0, is_audio_old = 0;
    uint8_t Rec_weather[3][3] = {0};
    int8_t Rec_action = 0; //翻页
    int8_t page_index = 0;
    int8_t changing = 0;   //表示动画切换
    while(1)
    {
        portBASE_TYPE xStatue;
        UBaseType_t queue_num;
        //queue_num = uxQueueMessagesWaiting(xQueue_led_dft);
        //ESP_LOGI(TAG_CAR, "queue num is %d", queue_num);
        //xStatue = xQueueReceive(xQueue_time, Rec_Buf, portMAX_DELAY);
        //if(xStatue == pdPASS)
        //{
        //    ESP_LOGI(TAG_CAR, "Rec_Buf is %s", Rec_Buf);
        //}
        //char test[64] = {0};
        //queue_num = uxQueueMessagesWaiting(xQueue_time);
        //ESP_LOGI(TAG_CAR, "time queue num is %d", queue_num);
        //xStatue = xQueueReceive(xQueue_time, Rec_time, 0);
        xStatue = xQueueReceive(xQueue_weather, Rec_weather, 0);
        
        xStatue = xQueueReceive(xQueue_is_audio, &is_audio, 0);
        if(xStatue == pdPASS)
        {
            if(is_audio > is_audio_old)//切换的过程
            {
                vTaskSuspend(xHandle_tcp);
                frequency_spectrum_refresh(strip, led_dft_num);
                ESP_ERROR_CHECK(strip->refresh(strip, 100));
            }
            else if(is_audio < is_audio_old)
            {
                vTaskResume(xHandle_tcp);
                ESP_ERROR_CHECK(strip->clear(strip, 100));//全部清零
            }
            is_audio_old = is_audio;
        }
        
        if(is_audio)    //音频模式
        {
            
            //ESP_LOGI("TAG_CAR", "is audio");
            xStatue = xQueueReceive(xQueue_led_dft, led_dft_num, 100/portTICK_PERIOD_MS);
            if(xStatue == pdPASS)
            {
                ESP_LOGI(TAG_CAR, "enter led strip");
                frequency_spectrum_refresh(strip, led_dft_num);
                ESP_ERROR_CHECK(strip->refresh(strip, 100));
            }
        }
        else        //非音频模式
        {      
            //ESP_LOGI("TAG_CAR", "is not audio");
            xStatue = xQueueReceive(xQueue_page, &Rec_action, 0);
            if(xStatue == pdPASS)
            {
                if(Rec_action == -1)//左
                {
                    changing = -32;
                }
                else if(Rec_action == 1)//右
                {
                    changing = 32;
                }
                page_index = page_index + Rec_action;
                if(page_index < -1)
                    page_index = -1;
                if(page_index > 3)
                    page_index = 3;
            }
            if(changing)    //切换动画
            {
                if(changing > 0)
                {
                    if(page_index == 0)
                    {
                        ESP_ERROR_CHECK(strip->clear(strip, 100));//全部清零
                        data_refresh(strip, Rec_time, -(32-changing));
                        time_refresh(strip, Rec_time, changing-1);
                        ESP_ERROR_CHECK(strip->refresh(strip, 100));
                    }
                    if(page_index == 1)
                    {
                        ESP_ERROR_CHECK(strip->clear(strip, 100));//全部清零
                        time_refresh(strip, Rec_time, -(32-changing));
                        weather_refresh(strip, *(Rec_weather + page_index -1), changing-1);
                        ESP_ERROR_CHECK(strip->refresh(strip, 100));
                    }
                    if(page_index == 2 || page_index == 3)
                    {
                        ESP_ERROR_CHECK(strip->clear(strip, 100));//全部清零
                        weather_refresh(strip, *(Rec_weather + page_index -1 -1), -(32-changing));
                        weather_refresh(strip, *(Rec_weather + page_index -1), changing-1);
                        ESP_ERROR_CHECK(strip->refresh(strip, 100));
                    }
                    changing--;
                    vTaskDelay(40/portTICK_PERIOD_MS);
                }
                else if(changing < 0)
                {
                    if(page_index == -1)
                    {
                        ESP_ERROR_CHECK(strip->clear(strip, 100));//全部清零
                        data_refresh(strip, Rec_time, changing+1);
                        time_refresh(strip, Rec_time, changing+32+1);
                        ESP_ERROR_CHECK(strip->refresh(strip, 100));
                    }
                    if(page_index == 0)
                    {
                        ESP_ERROR_CHECK(strip->clear(strip, 100));//全部清零
                        time_refresh(strip, Rec_time, changing+1);
                        weather_refresh(strip, *(Rec_weather + page_index -1), changing+32+1);
                        ESP_ERROR_CHECK(strip->refresh(strip, 100));
                    }
                    if(page_index == 1 || page_index == 2)
                    {
                        ESP_ERROR_CHECK(strip->clear(strip, 100));//全部清零
                        weather_refresh(strip, *(Rec_weather + page_index-1 -1), changing+1);
                        weather_refresh(strip, *(Rec_weather + page_index -1), changing+32+1);
                        ESP_ERROR_CHECK(strip->refresh(strip, 100));
                    }
                    changing++;
                    vTaskDelay(40/portTICK_PERIOD_MS);
                }
            }
            else //稳定显示
            {
                if(page_index == -1)
                {
                    xStatue = xQueueReceive(xQueue_time, Rec_time, 0);
                    if(xStatue == pdPASS)
                    {
                        data_refresh(strip, Rec_time, 0);
                        ESP_ERROR_CHECK(strip->refresh(strip, 100));
                    }
                }
                else if(page_index == 0)
                {
                    xStatue = xQueueReceive(xQueue_time, Rec_time, 0);
                    if(xStatue == pdPASS)
                    {
                        time_refresh(strip, Rec_time, 0);
                        ESP_ERROR_CHECK(strip->refresh(strip, 100));
                    }
                }
                else if(page_index > 0)
                {
                    xStatue = xQueueReceive(xQueue_weather, Rec_weather, 0);
                    if(xStatue == pdPASS)
                    {
                        weather_refresh(strip, *(Rec_weather + page_index -1), 0);
                        ESP_ERROR_CHECK(strip->refresh(strip, 100));
                    }
                }  
            }         
            
           /*
            xStatue = xQueueReceive(xQueue_weather, Rec_weather, 0);
            if(xStatue == pdPASS)
            {
                weather_refresh(strip, Rec_weather);
                ESP_ERROR_CHECK(strip->refresh(strip, 100));
            }
            */
        }
        //vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

static void DFT_task(void *parm)
{
    //uint16_t dft_test[16] = {0};
    uint16_t dft_PCM[64] = {0};
    float amp[64] = {0};
    uint8_t amp_led[32] = {0};
    //for(int i = 0; i < 16; i++)
    //{
    //    dft_test[i] = 100 * sin(2*3.14159/16*i) + 100;
    //}
    while(1)
    {
        portBASE_TYPE xStatue;
        UBaseType_t queue_num;
        queue_num = uxQueueMessagesWaiting(xQueue_time);
        //ESP_LOGI(TAG_CAR, "queue num is %d", queue_num);
        xStatue = xQueueReceive(xQueue_PCM, dft_PCM, portMAX_DELAY);
        if(xStatue == pdPASS)
        {
            //ESP_LOGI(TAG_CAR, "Rec_Buf is %s", Rec_Buf);
            //for(int i = 0; i < 64; i++)
            //{
            //    printf("%d, ", *(dft_PCM+i));
            //}
            //printf("\n");
            FFT_Cal(&dft_PCM[0], &amp[0]);
            for(int i = 0; i < 32; i++)
            {
                //printf("%f, ", *(amp+i));
                //amp_led[i] = *(amp+i)*8 / (255*64);//对幅值进行归一化，最大值为64×255，最后乘以8是最多8个灯。乘以8不行啊，高频部分太小
                amp_led[i] = *(amp+i+1) / (255);//把第一列舍了，太大
                if(amp_led[i] > 7)
                    amp_led[i] = 7;
                //printf("%d, \n", amp_led[i]);
            }
            xQueueSendToBack(xQueue_led_dft, amp_led, 0);
        }
        //ESP_LOGI(TAG_CAR, "Going to calculate DFT");
        //DFT_Cal(&dft_test[0], &amp[0]);
        //for(int i = 0; i < 16; i++)
        //{
        //    printf("%d\n", dft_test[i]);
        //}
        //for(int i = 0; i < 16; i++)
        //{
        //    printf("%d:%f\n", i, *(amp+i));
        //}
        //vTaskDelay(1000/portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .host = "mqtt.heclouds.com",
        .port = 6002,
        .client_id = "646520531",
        .username = "383448",
        .password = "mp1",
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
}

void bt_mp3_task(void *parm)
{
    uint8_t is_audio = 0;
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t bt_stream_reader, i2s_stream_writer;

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    ESP_LOGI(TAG, "[ 1 ] Create Bluetooth service");
    bluetooth_service_cfg_t bt_cfg = {
        .device_name = "Magic Pixel",
        .mode = BLUETOOTH_A2DP_SINK,
    };
    bluetooth_service_start(&bt_cfg);

    ESP_LOGI(TAG, "[ 2 ] Start codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);

    ESP_LOGI(TAG, "[ 3 ] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);

    ESP_LOGI(TAG, "[3.1] Create i2s stream to write data to codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "[3.2] Get Bluetooth stream");
    bt_stream_reader = bluetooth_service_create_stream();

    ESP_LOGI(TAG, "[3.2] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, bt_stream_reader, "bt");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    ESP_LOGI(TAG, "[3.3] Link it together [Bluetooth]-->bt_stream_reader-->i2s_stream_writer-->[codec_chip]");

#if (CONFIG_ESP_LYRATD_MSC_V2_1_BOARD || CONFIG_ESP_LYRATD_MSC_V2_2_BOARD)
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = 44100;
    rsp_cfg.src_ch = 2;
    rsp_cfg.dest_rate = 48000;
    rsp_cfg.dest_ch = 2;
    audio_element_handle_t filter = rsp_filter_init(&rsp_cfg);
    audio_pipeline_register(pipeline, filter, "filter");
    i2s_stream_set_clk(i2s_stream_writer, 48000, 16, 2);
    const char *link_tag[3] = {"bt", "filter", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);
#else
    const char *link_tag[2] = {"bt", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 2);
#endif
    ESP_LOGI(TAG, "[ 4 ] Initialize peripherals");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    ESP_LOGI(TAG, "[4.1] Initialize Touch peripheral");
    //audio_board_key_init(set);

    ESP_LOGI(TAG, "[4.2] Create Bluetooth peripheral");
    esp_periph_handle_t bt_periph = bluetooth_service_create_periph();

    ESP_LOGI(TAG, "[4.2] Start all peripherals");
    esp_periph_start(set, bt_periph);

    ESP_LOGI(TAG, "[ 5 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[5.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[5.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 6 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);

    ESP_LOGI(TAG, "[ 7 ] Listen for all pipeline events");
    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        if (msg.cmd == AEL_MSG_CMD_ERROR) {
            ESP_LOGE(TAG, "[ * ] Action command error: src_type:%d, source:%p cmd:%d, data:%p, data_len:%d",
                     msg.source_type, msg.source, msg.cmd, msg.data, msg.data_len);
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) bt_stream_reader
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(bt_stream_reader, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from Bluetooth, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);
            
            is_audio = 1;
            xQueueSendToBack(xQueue_is_audio, &is_audio, 0);

            audio_element_setinfo(i2s_stream_writer, &music_info);
#if (CONFIG_ESP_LYRATD_MSC_V2_1_BOARD || CONFIG_ESP_LYRATD_MSC_V2_2_BOARD)
#else
            i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
#endif
            continue;
        }

        if ((msg.source_type == PERIPH_ID_TOUCH || msg.source_type == PERIPH_ID_BUTTON || msg.source_type == PERIPH_ID_ADC_BTN)
            && (msg.cmd == PERIPH_TOUCH_TAP || msg.cmd == PERIPH_BUTTON_PRESSED || msg.cmd == PERIPH_ADC_BUTTON_PRESSED)) {

            if ((int) msg.data == get_input_play_id()) {
                ESP_LOGI(TAG, "[ * ] [Play] touch tap event");
                periph_bluetooth_play(bt_periph);
            } else if ((int) msg.data == get_input_set_id()) {
                ESP_LOGI(TAG, "[ * ] [Set] touch tap event");
                periph_bluetooth_pause(bt_periph);
            } else if ((int) msg.data == get_input_volup_id()) {
                ESP_LOGI(TAG, "[ * ] [Vol+] touch tap event");
                periph_bluetooth_next(bt_periph);
            } else if ((int) msg.data == get_input_voldown_id()) {
                ESP_LOGI(TAG, "[ * ] [Vol-] touch tap event");
                periph_bluetooth_prev(bt_periph);
            }
        }

        /* Stop when the Bluetooth is disconnected or suspended */
        if (msg.source_type == PERIPH_ID_BLUETOOTH
            && msg.source == (void *)bt_periph) {
            if (msg.cmd == PERIPH_BLUETOOTH_DISCONNECTED) {
                ESP_LOGW(TAG, "[ * ] Bluetooth disconnected");
                //break;
                is_audio = 0;
                xQueueSendToBack(xQueue_is_audio, &is_audio, 0);
            }
        }
        /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) i2s_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
            ESP_LOGW(TAG, "[ * ] Stop event received");
            //break;
        }

        
    }

    ESP_LOGI(TAG, "[ 8 ] Stop audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    audio_pipeline_unregister(pipeline, bt_stream_reader);
    audio_pipeline_unregister(pipeline, i2s_stream_writer);

    /* Terminate the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline);

    /* Stop all peripherals before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */

#if (CONFIG_ESP_LYRATD_MSC_V2_1_BOARD || CONFIG_ESP_LYRATD_MSC_V2_2_BOARD)
    audio_pipeline_unregister(pipeline, filter);
    audio_element_deinit(filter);
#endif
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(bt_stream_reader);
    audio_element_deinit(i2s_stream_writer);
    esp_periph_set_destroy(set);
    bluetooth_service_destroy();
    vTaskDelete(NULL);
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
        ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
        ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler));
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
        ESP_LOGI(TAG, "start smart config -by car");//开始smartconfig

        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
        ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
        bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    if(bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG_CAR, "wifi connected.");
        
        initialize_sntp();
    }
    //vEventGroupDelete(s_wifi_event_group);
}
static void initialize_sntp(void)
{
    //开机打印
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGI(TAG, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
    }
    //初始化sntp
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    //sntp_set_time_sync_notification_cb(time_sync_notification_cb);
#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
#endif
    sntp_init();
    // Set timezone to China Standard Time
    char strftime_buf[64];
    setenv("TZ", "CST-8", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in Shanghai is: %s", strftime_buf);
    //获取时间
    //time_t now = 0;
    //struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 15;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in Shanghai is: %s", strftime_buf);
    ESP_LOGI(TAG_CAR, "sntp task start.");
    
    
    mqtt_app_start();
    xTaskCreate(sntp_get_time_task, "sntp_get_time_task", 4096, NULL, 2, &xHandle_sntp);
    xTaskCreate(tcp_client_task, "tcp_client_task", 4096, NULL, 0, &xHandle_tcp);   //优先级改成0就可以和蓝牙兼容了
    xTaskCreatePinnedToCore(led_strip_task, "led_strip_task", 4096, NULL, 4, NULL, 1);
    //xTaskCreate(led_strip_task, "led_strip_task", 4096, NULL, 4, NULL);
    xTaskCreate(bt_mp3_task, "bt_mp3_task", 4096, NULL, 3, NULL);
    xTaskCreatePinnedToCore(DFT_task, "DFT_task", 4096, NULL, 3, NULL, 1);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(ret);

    xQueue_time = xQueueCreate(3, 64*sizeof(char));
    xQueue_PCM = xQueueCreate(3, 64*sizeof(uint16_t));
    xQueue_led_dft = xQueueCreate(2, 32*sizeof(uint8_t));
    xQueue_is_audio = xQueueCreate(2, sizeof(uint8_t));
    xQueue_weather = xQueueCreate(2, 9*sizeof(uint8_t));
    xQueue_page = xQueueCreate(2, sizeof(int8_t));
    if(xQueue_time == NULL)
    {
        ESP_LOGI(TAG_CAR, "creat queue failed.");
        while(1);
    }
    wifi_init_sta();
    //xTaskCreate(bt_mp3_task, "bt_mp3_task", 4096, NULL, 5, NULL);
}
