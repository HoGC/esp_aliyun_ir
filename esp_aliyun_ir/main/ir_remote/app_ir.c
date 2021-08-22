#include "app_ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_spiffs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ir/ir.h"
#include "ir/raw.h"
#include "ir/generic.h"
#include "http_client.h"

static const char *TAG = "app_ir";

#define HTTP_PREFIX "http://irext-debug.oss-cn-hangzhou.aliyuncs.com/irda_"
#define HTTP_PREFIX_LEN (sizeof(HTTP_PREFIX) - 1U) 
#define HTTP_SUFFIX ".bin"
#define HTTP_SUFFIX_LEN (sizeof(HTTP_SUFFIX) - 1U)

#define URL_MAX_LEN      128

int irext_bin_download(const char *protocol, int control_code, char *file_buf, int file_buf_size, int *file_size){

    char url[URL_MAX_LEN] = {0};
    char control_code_str[6] = {0};

    httpclient_t http_client = {0};
    httpclient_data_t client_data = {0};

    strcpy(url, HTTP_PREFIX);
    strcat(url, protocol);
    strcat(url, "_");

    sprintf(control_code_str, "%d", control_code);
    strcat(url, control_code_str);
    strcat(url, ".bin");

    ESP_LOGI(TAG, "irext_bin_download_url: %s", url);

    client_data.post_buf_len = 0;
    client_data.response_buf = file_buf;
    if(!client_data.response_buf){
        ESP_LOGI(TAG, "malloc fail");
        return -1;
    }
    client_data.response_buf_len = file_buf_size;

    httpclient_set_custom_header(&http_client, "");

    HTTPC_RESULT http_ret = httpclient_get_request(&http_client, url, &client_data, NULL);
    if(http_ret != HTTP_SUCCESS){
        ESP_LOGI(TAG, "httpclient get file fail result: %d", http_ret);
        return -1;
    }
    *file_size = client_data.response_len;
    return 0;
}


static void ir_pulses_print_u16(uint16_t *buffer, uint16_t len){

    int symbol = 0;

    for (int i=0; i < len; i++) {
        if (symbol == 0){
            printf("%5d ", buffer[i]);
        }else{
            printf("%5d ", (-1) * buffer[i]);
        }
        symbol = (symbol+1)%2;
        if (i % 16 == 15)
            printf("\n");
    }
    if (len % 16)
        printf("\n");
}

static void ir_pulses_print_i32(int32_t *buffer, uint16_t len){

    for (int i=0; i < len; i++) {
        printf("%5d ", buffer[i]);
        if (i % 16 == 15)
            printf("\n");
    }
    if (len % 16)
        printf("\n");
}

static void ir_pulses_send(uint16_t *buffer, uint16_t len){

    int symbol = 0;
    int buffer_len = 0;

    int32_t *buffer1 = malloc(sizeof(int32_t) * len);
    
    for (int i=0; i < len; i++) {
        if (symbol == 0){
            buffer1[buffer_len] = buffer[i];
        }else{
            buffer1[buffer_len] = (-1) * buffer[i];
        }
        symbol = (symbol+1)%2;
        buffer_len++;
        if(i%16 == 15){
            ir_raw_send(buffer1, buffer_len);
            buffer_len = 0;
        }
    }
    ir_raw_send(buffer1, buffer_len);
    free(buffer1);
}


int app_ir_remote_status_send(const char *protocol, int control_code, remote_status_t *status){

    int file_size = 0;
    char path[128] = {0};
    char file_buf[512] = {0};
    uint16_t decoded[1024] = {0};

    sprintf(path, "/irext_bin/%s_%d", protocol, control_code);

    struct stat st;
    if (stat(path, &st) != 0) {

        FILE* f = fopen(path, "wb");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open file for writeing");
            return -1;
        }

        irext_bin_download(protocol, control_code, file_buf, 1024, &file_size);
        fwrite(file_buf, 1, file_size, f);
        fclose(f);
    }else{
        FILE* f = fopen(path, "rb");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open file for reading");
            return -1;
        }

        file_size = fread(file_buf, 1, sizeof(file_buf), f);
        fclose(f);

        if(file_size == 0){
            FILE* f = fopen(path, "wb");
            if (f == NULL) {
                ESP_LOGE(TAG, "Failed to open file for writeing");
                return -1;
            }

            irext_bin_download(protocol, control_code, file_buf, 1024, &file_size);
            fwrite(file_buf, 1, file_size, f);
            fclose(f);
        }
    }

    ir_binary_open(REMOTE_CATEGORY_AC, 1, (unsigned char *)file_buf, file_size);

    uint16_t decode_len = 0;
    decode_len = ir_decode(0, decoded, status, 0);
	ir_close();	

    // ir_pulses_print_u16(decoded, decode_len);
    ir_pulses_send(decoded, decode_len);    
    return 0;
}

static int match(int32_t actual, int32_t expected, uint8_t tolerance) {
    if (actual < 0) {
        if (expected > 0) {
            return 0;
        }
        actual = -actual + 50;
        expected = -expected;
    } else {
        if (expected < 0) {
            return 0;
        }
        actual -= 50;
    }

    uint16_t delta = ((uint32_t)expected) * tolerance / 100;
    if ((actual < expected - delta) || (expected + delta < actual)) {
        return 0;
    }

    return 1;
}

static int ir_pulses_decode(ir_generic_config_t config,
                             int32_t *pulses, uint16_t count,
                             void *data, uint16_t data_size)
{
    ESP_LOGI(TAG, "generic: ir generic decode size = %d", count);

    if (!data_size) {
        ESP_LOGI(TAG, "generic: invalid buffer size");
        return -1;
    }

    if (!match(pulses[0], config.header_mark, config.tolerance) ||
           !match(pulses[1], config.header_space, config.tolerance))
    {
        ESP_LOGI(TAG, "generic: header does not match");
        return 0;
    }

    uint8_t *bits = data;
    uint8_t *bits_end = data + data_size;

    *bits = 0;

    uint8_t bit_count = 0;
    for (int i=2; i + 1 < count; i+=2, bit_count++) {
        if (bit_count >= 8) {
            bits++;
            *bits = 0;

            bit_count = 0;
        }

        if (match(pulses[i], config.bit1_mark, config.tolerance) &&
                match(pulses[i+1], config.bit1_space, config.tolerance)) {

            if (bits == bits_end) {
                ESP_LOGI(TAG, "generic: data overflow");
                return -1;
            }

            *bits |= 1 << bit_count;
        } else if (match(pulses[i], config.bit0_mark, config.tolerance) &&
                match(pulses[i+1], config.bit0_space, config.tolerance)) {

            if (bits == bits_end) {
                ESP_LOGI(TAG, "generic: data overflow");
                return -1;
            }

            *bits |= 0 << bit_count;
        } else if (match(pulses[i], config.footer_mark, config.tolerance) &&
                match(pulses[i+1], config.footer_space, config.tolerance)) {

            if (bits == bits_end) {
                ESP_LOGI(TAG, "generic: data overflow");
                return -1;
            }

            bit_count = 8;

        } else {
            if(i + 3 < count){
                if (match(pulses[i+2], config.header_mark, config.tolerance) &&
                        match(pulses[i+3], config.header_space, config.tolerance)) {
                    i+=2;
                    bit_count = 8;
                    continue;
                }
            }
            ESP_LOGI(TAG, "generic: pulses at %d does not match: %d %d",
                    i, pulses[i], pulses[i+1]);
            return (bits - (uint8_t*)data + (bit_count ? 1 : 0));
        }
    }

    int decoded_size = bits - (uint8_t*)data + (bit_count ? 1 : 0);
    ESP_LOGI(TAG, "generic: decoded %d bytes", decoded_size);
    return decoded_size;
}

static ir_generic_config_t ac_protocol_config = {
    .header_mark = 8967,
    .header_space = -4499,

    .bit1_mark = 710,
    .bit1_space = -1590,

    .bit0_mark = 710,
    .bit0_space = -460,

    .footer_mark = 717,
    .footer_space = -19984,

    .tolerance = 10,
};

static void ir_rx_task(void *arg) {

    ESP_LOGI(TAG, "ir_rx_task");

    uint16_t buffer_size = sizeof(int32_t) * 1024;
    int32_t *buffer = malloc(buffer_size);

    ir_decoder_t *raw_decoder = ir_raw_make_decoder();
    while (1) {
        int size = ir_recv(raw_decoder, 0, buffer, buffer_size);
        if (size <= 0)
            continue;

        ir_pulses_print_i32(buffer, size);

        uint8_t data[32] = {0};
        size = ir_pulses_decode(ac_protocol_config, buffer, buffer_size, data, 32);
        for (int i=0; i < size; i++) {
            printf("0x%02x ", data[i]);
            if (i % 16 == 15)
                printf("\n");
        }

        if (size % 16)
            printf("\n");
        }
}


void app_ir_remote_init(void){

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/irext_bin",
      .partition_label = NULL,
      .max_files = 25,
      .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGI(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGI(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGI(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    ir_driver_tx_init();
    // ir_driver_rx_init(12, 1024);

    // xTaskCreate(ir_rx_task, "ir_rx", 2048, NULL, 1, NULL);
}