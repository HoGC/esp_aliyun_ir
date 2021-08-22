
#include "app_ota.h"

#include <string.h>

#include "esp_log.h"
#include "esp_ota_ops.h"

#include "http_client.h"

static const char *TAG = "app_ota";

static int ots_schedule = 0;
static esp_ota_handle_t update_handle = 0;
static const esp_partition_t *update_partition = NULL;

uint8_t __ota_get_schedule(void){
    uint8_t schedule = 0;
    schedule = ots_schedule;
    return schedule;
}


int __ota_update(uint8_t *buf, uint16_t len)
{    
    esp_err_t err = ESP_OK;
    err = esp_ota_write(update_handle, (const void *)buf, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write error: %d", err);
        return -1;
    }
    return 0;
}

int __ota_end(void)
{
    esp_err_t ota_end_err = esp_ota_end(update_handle);
	if (ota_end_err != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_end failed! err=0x%d. Image is invalid", ota_end_err);
		return -1;
	}

	esp_err_t err = esp_ota_set_boot_partition(update_partition);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_set_boot_partition failed! err=0x%d", err);
		return -1;
	}
	ESP_LOGI(TAG, "esp_ota_set_boot_partition succeeded");
    return 0;
}


int __ota_begin(void)
{
    update_partition = esp_ota_get_next_update_partition(NULL);
	if (update_partition == NULL) {
		ESP_LOGE(TAG, "Passive OTA partition not found");
		return -1;
	}

	ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
	         update_partition->subtype, update_partition->address);

	esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_begin failed, error=%d", err);
		return 0;
	}

	ESP_LOGI(TAG, "esp_ota_begin succeeded");
	ESP_LOGI(TAG, "Please Wait. This may take time");

    return 0;
}



static HTTPC_RESULT http_recv_cb(httpclient_t *client, httpclient_data_t *client_data){

    ots_schedule = (client_data->response_content_len - client_data->retrieve_len) *100 / client_data->response_content_len;

    ESP_LOGI(TAG, "ots_schedule: %d", ots_schedule);
    int err = __ota_update((uint8_t *)client_data->response_buf, client_data->response_len);
    if(err != 0){
        return HTTP_ERECV;
    }

    return HTTP_SUCCESS;
}

static void http_connect_cb(httpclient_t *client){
    __ota_begin();
}

static void http_close_cb(httpclient_t *client){
    
}

int app_ota_atart(char *path){

    static char ota_path[128] = {0};
    static char oat_resp_buf[1024] = {0};

    httpclient_cb_t client_cb = {0};
    httpclient_t http_client = {0};
    httpclient_data_t client_data = {0};

    strcpy(ota_path, path);

    http_client.user_arg = NULL;
    
    client_data.post_buf = NULL;
    client_data.post_buf_len = 0;
    client_data.response_buf = oat_resp_buf;
    client_data.response_buf_len = 1024;
    httpclient_set_custom_header(&http_client, "");

    client_cb.connect_cb = http_connect_cb;
    client_cb.recv_cb = http_recv_cb;
    client_cb.close_cb = http_close_cb;
    
    HTTPC_RESULT err = httpclient_get_request(&http_client, ota_path, &client_data, &client_cb);
   
    if(err == HTTP_SUCCESS){
        if(http_client.response_code == 200){
            __ota_end();
            return 0;
        }
    }

    return -1;
}