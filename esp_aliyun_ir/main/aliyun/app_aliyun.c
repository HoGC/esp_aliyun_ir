#include "app_aliyun.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "cJSON.h"

#include "infra_types.h"
#include "infra_defs.h"
#include "dev_model_api.h"

#include "conn_mgr.h"
#include "dm_wrapper.h"

#include "qrcode.h"

// #define QR_URL_PREFIX   "https://g.aliplus.com/ilop/d.html?locale=all&pk="
#define QR_URL_PREFIX   "https://g.aliplus.com/ilop/d.html?locale=zh-CN&pk="            //国内


static const char *TAG = "app aliyun";

static bool _linkkit_is_run = false;
static bool _connect_status = false;

static aliyun_event_cb_t _event_cb = NULL;
static aliyun_property_set_cb_t _set_property_cb = NULL;
static aliyun_property_get_cb_t _get_property_cb = NULL;

static char PRODUCT_KEY[IOTX_PRODUCT_KEY_LEN + 1] = {0};
static char PRODUCT_SECRET[IOTX_DEVICE_SECRET_LEN + 1] = {0};
static char DEVICE_NAME[IOTX_DEVICE_NAME_LEN + 1] = {0};
static char DEVICE_SECRET[IOTX_DEVICE_SECRET_LEN + 1] = {0};


static int user_property_set_event_handler(const int devid, const char *request, const int request_len)
{
    int res = 0;
    ESP_LOGI(TAG,"Property Set Received, Devid: %d, Request: %s", devid, request);
    
    if (!request) {
        return NULL_VALUE_ERROR;
    }

    if(_set_property_cb){
        _set_property_cb(request, request_len);
    }

    res = IOT_Linkkit_Report(devid, ITM_MSG_POST_PROPERTY,
                             (unsigned char *)request, request_len);
    ESP_LOGI(TAG,"Post Property Message ID: %d", res);
    
    return SUCCESS_RETURN;
}

static int user_property_get_event_handler(const int devid, const char *serviceid, const int serviceid_len, char **response, int *response_len)
{
    ESP_LOGI(TAG,"Get Property Message ID: %d", devid);

    if(_get_property_cb){
        _get_property_cb(serviceid, serviceid_len, response, response_len);
    }

    return SUCCESS_RETURN;
}

static int user_connected_event_handler(void)
{
    _connect_status = true;
    _event_cb(ALIYUN_CLOULD_CONNECTED);
    return 0;
}

/** cloud disconnected event callback */
static int user_disconnected_event_handler(void)
{
    _connect_status = false;
    _event_cb(ALIYUN_CLOULD_DISCONNECTED);
    return 0;
}


static void linkkit_thread(void *args)
{
    int res = 0;
    iotx_linkkit_dev_meta_info_t master_meta_info;
    int master_devid = 0, domain_type = 0, dynamic_register = 0, post_reply_need = 0;

    memset(&master_meta_info, 0, sizeof(iotx_linkkit_dev_meta_info_t));
    memcpy(master_meta_info.product_key, PRODUCT_KEY, strlen(PRODUCT_KEY));
    memcpy(master_meta_info.product_secret, PRODUCT_SECRET, strlen(PRODUCT_SECRET));
    memcpy(master_meta_info.device_name, DEVICE_NAME, strlen(DEVICE_NAME));
    memcpy(master_meta_info.device_secret, DEVICE_SECRET, strlen(DEVICE_SECRET));

    /* Register Callback */
    IOT_RegisterCallback(ITE_CONNECT_SUCC, user_connected_event_handler);
    IOT_RegisterCallback(ITE_DISCONNECTED, user_disconnected_event_handler);
    IOT_RegisterCallback(ITE_PROPERTY_SET, user_property_set_event_handler);
    IOT_RegisterCallback(ITE_PROPERTY_GET, user_property_get_event_handler);

#if CONFIG_MQTT_DIRECT
    domain_type = IOTX_CLOUD_REGION_SHANGHAI;
#else
    domain_type = IOTX_CLOUD_REGION_SINGAPORE;
#endif

    IOT_Ioctl(IOTX_IOCTL_SET_DOMAIN, (void *)&domain_type);

    /* Choose Login Method */
    dynamic_register = 0;
    IOT_Ioctl(IOTX_IOCTL_SET_DYNAMIC_REGISTER, (void *)&dynamic_register);

    /* post reply doesn't need */
    post_reply_need = 1;
    IOT_Ioctl(IOTX_IOCTL_RECV_EVENT_REPLY, (void *)&post_reply_need);

    /* Create Master Device Resources */
    master_devid = IOT_Linkkit_Open(IOTX_LINKKIT_DEV_TYPE_MASTER, &master_meta_info);
    if (master_devid < 0) {
        ESP_LOGE(TAG, "IOT_Linkkit_Open Failed\n");
        vTaskDelete(NULL);
        return;
    }

    /* Start Connect Aliyun Server */
    res = IOT_Linkkit_Connect(master_devid);
    if (res < 0) {
        ESP_LOGE(TAG, "IOT_Linkkit_Connect Failed\n");
        IOT_Linkkit_Close(master_devid);
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        IOT_Linkkit_Yield(200);
    }

    IOT_Linkkit_Close(master_devid);

    IOT_DumpMemoryStats(IOT_LOG_DEBUG);
    IOT_SetLogLevel(IOT_LOG_NONE);
    
    vTaskDelete(NULL);
}

static esp_err_t wifi_event_handle(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_GOT_IP:
            if (_linkkit_is_run == false) {
                wifi_config_t wifi_config = {0};
                if (conn_mgr_get_wifi_config(&wifi_config) == ESP_OK &&
                    strcmp((char *)(wifi_config.sta.ssid), HOTSPOT_AP) &&
                    strcmp((char *)(wifi_config.sta.ssid), ROUTER_AP)) {
                    xTaskCreate((void (*)(void *))linkkit_thread, "linkkit_thread", 1024*10, NULL, 5, NULL);
                    _linkkit_is_run = true;
                }
            }
            break;

        default:
            break;
    }

    return ESP_OK;
}

static void linkkit_event_monitor(int event)
{
    switch (event) {
        case IOTX_AWSS_START: // AWSS start without enbale, just supports device discover
            // operate led to indicate user
            ESP_LOGI(TAG, "IOTX_AWSS_START");
            break;

        case IOTX_AWSS_ENABLE: // AWSS enable, AWSS doesn't parse awss packet until AWSS is enabled.
            ESP_LOGI(TAG, "IOTX_AWSS_ENABLE");
            // operate led to indicate user
            break;

        case IOTX_AWSS_LOCK_CHAN: // AWSS lock channel(Got AWSS sync packet)
            ESP_LOGI(TAG, "IOTX_AWSS_LOCK_CHAN");
            // operate led to indicate user
            break;

        case IOTX_AWSS_PASSWD_ERR: // AWSS decrypt passwd error
            ESP_LOGE(TAG, "IOTX_AWSS_PASSWD_ERR");
            // operate led to indicate user
            break;

        case IOTX_AWSS_GOT_SSID_PASSWD:
            ESP_LOGI(TAG, "IOTX_AWSS_GOT_SSID_PASSWD");
            // operate led to indicate user
            break;

        case IOTX_AWSS_CONNECT_ADHA: // AWSS try to connnect adha (device
            // discover, router solution)
            ESP_LOGI(TAG, "IOTX_AWSS_CONNECT_ADHA");
            // operate led to indicate user
            break;

        case IOTX_AWSS_CONNECT_ADHA_FAIL: // AWSS fails to connect adha
            ESP_LOGE(TAG, "IOTX_AWSS_CONNECT_ADHA_FAIL");
            // operate led to indicate user
            break;

        case IOTX_AWSS_CONNECT_AHA: // AWSS try to connect aha (AP solution)
            ESP_LOGI(TAG, "IOTX_AWSS_CONNECT_AHA");
            // operate led to indicate user
            break;

        case IOTX_AWSS_CONNECT_AHA_FAIL: // AWSS fails to connect aha
            ESP_LOGE(TAG, "IOTX_AWSS_CONNECT_AHA_FAIL");
            // operate led to indicate user
            break;

        case IOTX_AWSS_SETUP_NOTIFY: // AWSS sends out device setup information
            // (AP and router solution)
            ESP_LOGI(TAG, "IOTX_AWSS_SETUP_NOTIFY");
            // operate led to indicate user
            break;

        case IOTX_AWSS_CONNECT_ROUTER: // AWSS try to connect destination router
            ESP_LOGI(TAG, "IOTX_AWSS_CONNECT_ROUTER");
            // operate led to indicate user
            break;

        case IOTX_AWSS_CONNECT_ROUTER_FAIL: // AWSS fails to connect destination
            // router.
            ESP_LOGE(TAG, "IOTX_AWSS_CONNECT_ROUTER_FAIL");
            // operate led to indicate user
            break;

        case IOTX_AWSS_GOT_IP: // AWSS connects destination successfully and got
            // ip address
            ESP_LOGI(TAG, "IOTX_AWSS_GOT_IP");
            // operate led to indicate user
            break;

        case IOTX_AWSS_SUC_NOTIFY: // AWSS sends out success notify (AWSS
            // sucess)
            ESP_LOGI(TAG, "IOTX_AWSS_SUC_NOTIFY");
            // operate led to indicate user
            break;

        case IOTX_AWSS_BIND_NOTIFY: // AWSS sends out bind notify information to
            // support bind between user and device
            ESP_LOGI(TAG, "IOTX_AWSS_BIND_NOTIFY");
            // operate led to indicate user
            break;

        case IOTX_AWSS_ENABLE_TIMEOUT: // AWSS enable timeout
            // user needs to enable awss again to support get ssid & passwd of router
            ESP_LOGW(TAG, "IOTX_AWSS_ENALBE_TIMEOUT");
            // operate led to indicate user
            break;

        case IOTX_CONN_CLOUD: // Device try to connect cloud
            ESP_LOGI(TAG, "IOTX_CONN_CLOUD");
            // operate led to indicate user
            break;

        case IOTX_CONN_CLOUD_FAIL: // Device fails to connect cloud, refer to
            // net_sockets.h for error code
            ESP_LOGE(TAG, "IOTX_CONN_CLOUD_FAIL");
            // operate led to indicate user
            break;

        case IOTX_CONN_CLOUD_SUC: // Device connects cloud successfully
            ESP_LOGI(TAG, "IOTX_CONN_CLOUD_SUC");
            // operate led to indicate user
            break;

        case IOTX_RESET: // Linkkit reset success (just got reset response from
            // cloud without any other operation)
            ESP_LOGI(TAG, "IOTX_RESET");
            // operate led to indicate user
            break;

        default:
            break;
    }
}

int app_aliyun_post_property_str(char *str){
    int res = 0;

    res = IOT_Linkkit_Report(0, ITM_MSG_POST_PROPERTY,
                             (unsigned char *)str, strlen(str));
    ESP_LOGI(TAG, "Post Property Message ID: %d", res);
    return res;
}

void app_aliyun_register_set_property_cb(aliyun_property_set_cb_t set_property_cb){
    _set_property_cb = set_property_cb;
}

void app_aliyun_register_get_property_cb(aliyun_property_get_cb_t get_property_cb){
    _get_property_cb = get_property_cb;
}

static esp_err_t conn_mgr_is_configured(bool *configured)
{
    if (!configured) {
        return ESP_ERR_INVALID_ARG;
    }

    *configured = false;

    int ssid_len = 32;
    uint8_t ssid[32];

    int ret = HAL_Kv_Get(STA_SSID_KEY, ssid, &ssid_len);

    if (ret == ESP_OK && ssid_len) {
        *configured = true;
        ESP_LOGI(TAG, "Found ssid %s", ssid);
    }

    return ESP_OK;
}

bool app_aliyun_has_configured(void){
    bool configured = false;
    conn_mgr_is_configured(&configured);
    return configured;
    return false;
}

bool app_aliyun_get_connect_status(void){
    return _connect_status;
}

void app_aliyun_reset_config(void){
    conn_mgr_reset_wifi_config();
}

static void start_conn_mgr()
{
    iotx_event_regist_cb(linkkit_event_monitor);    // awss callback
    conn_mgr_start();

    vTaskDelete(NULL);
}

void app_aliyun_init(aliyun_event_cb_t event_cb){

    _event_cb = event_cb;

    conn_mgr_init();
    conn_mgr_register_wifi_event(wifi_event_handle);

    IOT_SetLogLevel(IOT_LOG_NONE);

    conn_mgr_set_sc_mode(CONN_SC_ZERO_MODE);

#ifdef CONFIG_MFG_USE_HARDCODED_CODE
    HAL_SetDeviceName(CONFIG_MFG_DEVICE_NAME);
    HAL_SetDeviceSecret(CONFIG_MFG_DEVICE_SECRET);
    HAL_SetProductKey(CONFIG_MFG_PRODUCT_KEY);
    HAL_SetProductSecret(CONFIG_MFG_PRODUCT_SECRET);
#endif

    HAL_GetProductKey(PRODUCT_KEY);
    HAL_GetProductSecret(PRODUCT_SECRET);
    HAL_GetDeviceName(DEVICE_NAME);
    HAL_GetDeviceSecret(DEVICE_SECRET);

    ESP_LOGI(TAG, "%s", "....................................................");
    ESP_LOGI(TAG, "%20s : %-s", "DeviceName", DEVICE_NAME);
    ESP_LOGI(TAG, "%20s : %-s", "DeviceSecret", DEVICE_SECRET);
    ESP_LOGI(TAG, "%20s : %-s", "ProductKey", PRODUCT_KEY);
    ESP_LOGI(TAG, "%20s : %-s", "ProductSecret", PRODUCT_SECRET);
    ESP_LOGI(TAG, "%s", "....................................................");

    char url[128] = {0};
    sprintf(url, "%s%s", QR_URL_PREFIX, PRODUCT_KEY);
    ESP_LOGI(TAG, "QR: %s", url);
    qrcode_display(url);

    xTaskCreate((void (*)(void *))start_conn_mgr, "conn_mgr", 3072, NULL, 4, NULL);
}

