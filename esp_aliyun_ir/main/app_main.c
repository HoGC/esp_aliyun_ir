#include <string.h>

#include "app_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"

#include "cJSON.h"

#include "flexible_button.h"

#include "app_ir.h"
#include "app_ota.h"
#include "app_shell.h"
#include "app_aliyun.h"


static const char *TAG = "app_main"; 

static uint16_t _led_flash_tick = 0;

void led_set_flash(uint16_t ms){
    _led_flash_tick = pdMS_TO_TICKS(ms);
}

void led_set_level(bool level){
    _led_flash_tick = 0;
    gpio_set_level(CONFIG_LED_GPIO, (uint8_t)((CONFIG_LED_ACTIVE_LEVEL)?(level):(!level)));
}

static void led_task(void *arg){

    uint32_t last_tick = 0;

    while (1){
        uint32_t curr_tick = xTaskGetTickCount();

        if(_led_flash_tick){
            if(curr_tick - last_tick > _led_flash_tick){
                last_tick = curr_tick;
                gpio_set_level(CONFIG_LED_GPIO, !gpio_get_level(CONFIG_LED_GPIO));
            }
        }
        vTaskDelay(10 / portTICK_RATE_MS);
    }
    vTaskDelete(NULL);
}

void led_init(void){


    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = 1ULL << CONFIG_LED_GPIO;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    led_set_level(0);

    xTaskCreate(led_task, "led_task", 2048, NULL, 7, NULL);
}

static uint8_t common_btn_read(void *arg)
{
    flex_button_t *btn = (flex_button_t *)arg;
    return gpio_get_level(btn->id);
}


static void common_btn_evt_cb(void *arg)
{
    flex_button_t *btn = (flex_button_t *)arg;
    
    ESP_LOGI(TAG, "id: [%d]  event: [%d]  repeat: %d", btn->id, btn->event, btn->click_cnt);

    switch (btn->event)
    {
    case FLEX_BTN_PRESS_LONG_START:
        app_aliyun_reset_config();
        vTaskDelay(500 / portTICK_RATE_MS);
        esp_restart();
        break;
    default:
        break;
    }
}

static void button_task(void *arg){
    while (1){
        flex_button_scan();
        vTaskDelay(FLEX_BTN_SCAN_FREQ_HZ / portTICK_RATE_MS);
    }
    vTaskDelete(NULL);
}

static void button_init(){
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << CONFIG_BUTTON_GPIO;
    io_conf.pull_down_en = CONFIG_BUTTON_ACTIVE_LEVEL;
    io_conf.pull_up_en = !CONFIG_BUTTON_ACTIVE_LEVEL;
    gpio_config(&io_conf);

    static flex_button_t button;

    button.id = CONFIG_BUTTON_GPIO;
    button.usr_button_read = common_btn_read;
    button.cb = common_btn_evt_cb;
    button.pressed_logic_level = 0;
    button.short_press_start_tick = FLEX_MS_TO_SCAN_CNT(1500);
    button.long_press_start_tick = FLEX_MS_TO_SCAN_CNT(3000);
    button.long_hold_start_tick = FLEX_MS_TO_SCAN_CNT(4500);

    flex_button_register(&button);

    xTaskCreate(button_task, "button_task", 2048, NULL, 6, NULL);
}

static void aliyun_event_cb(aliyun_event_id_t event)
{
    switch (event) {
        case ALIYUN_CLOULD_CONNECTED: 
            ESP_LOGI(TAG, "ALIYUN_CLOULD_CONNECTED");
            led_set_level(1);
            break;
        case ALIYUN_CLOULD_DISCONNECTED: 
            ESP_LOGI(TAG, "ALIYUN_CLOULD_DISCONNECTED");
            led_set_flash(500);
            break;
        default:
            break;
    }
}


static int aliyun_set_property_cb(const char *request, const int request_len){
    ESP_LOGI(TAG,"Property Set Received, Request: %s", request);

    static remote_status_t ac_status = {
        .ac_power = AC_POWER_ON,
        .ac_temp = AC_TEMP_25,
        .ac_mode = AC_MODE_COOL,
        .ac_wind_dir = AC_SWING_OFF,
        .ac_wind_speed = AC_WS_AUTO,
        .ac_display = 1,
        .ac_sleep = 0,
        .ac_timer = 0
    };

    cJSON *root = NULL, *item_powerstate = NULL, *item_temperature = NULL;

    root = cJSON_Parse(request);
    if (root == NULL || !cJSON_IsObject(root)) {
        ESP_LOGI(TAG, "JSON Parse Error");
        return -1;
    }

    item_powerstate = cJSON_GetObjectItem(root, "powerstate");
    if (item_powerstate != NULL && cJSON_IsNumber(item_powerstate)) {
        if (item_powerstate->valueint) {
            ac_status.ac_power = AC_POWER_ON;
        }else{
            ac_status.ac_power = AC_POWER_OFF;
        }
    }

    item_temperature = cJSON_GetObjectItem(root, "targetTemperature");
    if (item_temperature != NULL && cJSON_IsNumber(item_temperature)) {
        int temp = (int)item_temperature->valuedouble; 
        ac_status.ac_temp = AC_TEMP_16 + (temp-16);
    }
    
    cJSON_Delete(root);

    app_ir_remote_status_send("new_ac", 10727, &ac_status);

    return 0;
}

void app_main()
{
#ifdef CONFIG_ENABLE_SHELL
    app_shell_init();
#endif

    led_init();
    
    button_init();

    app_aliyun_init(aliyun_event_cb);
    app_aliyun_register_set_property_cb(aliyun_set_property_cb);

    if(app_aliyun_has_configured()){
        led_set_flash(500);
    }else{
        led_set_flash(100);
    }

    app_ir_remote_init();
}

#ifdef CONFIG_ENABLE_SHELL
static char ota_url[128] = "";
static char ota_status = 0;

static void ota_task(void *arg){
    
    ESP_LOGI(TAG, "OTA Path: %s", ota_url);

    ota_status = 1;

    int err = app_ota_atart(ota_url);
    if(err == 0){
        ESP_LOGE(TAG, "OTA Success Wait Restart");
        vTaskDelay(500 / portTICK_RATE_MS);
        esp_restart();
        vTaskDelay(100 / portTICK_RATE_MS);
    }else{
        ESP_LOGE(TAG, "OTA Fail");
    }

    ota_status = 0;

    vTaskDelete(NULL);
}

int shell_ota(int argc, char *agrv[])
{
    if(argc != 2){
        ESP_LOGE(TAG, "OTA Parm Error!");
        return -1;
    }

    if(ota_status){
        ESP_LOGE(TAG, "OTA Ongoing!");
        return -1;
    }

    sprintf(ota_url, "%s", agrv[1]);

    xTaskCreate(ota_task, "ota_task", 4096, NULL, 10, NULL);

    return 0;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), ota, shell_ota, ota);
#endif