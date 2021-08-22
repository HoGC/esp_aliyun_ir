#ifndef _APP_ALIYUN_H_
#define _APP_ALIYUN_H_
#include <stdint.h>
#include <stdbool.h>

#include "infra_compat.h"

typedef enum {    
    ALIYUN_CLOULD_CONNECTED,         
    ALIYUN_CLOULD_DISCONNECTED,         
} aliyun_event_id_t;


typedef void (*aliyun_event_cb_t)(aliyun_event_id_t event);
typedef int (*aliyun_property_set_cb_t)(const char *request, const int request_len);
typedef int (*aliyun_property_get_cb_t)(const char *serviceid, const int serviceid_len, char **response, int *response_len);

bool app_aliyun_has_configured(void);

void app_aliyun_reset_config(void);

bool app_aliyun_get_connect_status(void);

int app_aliyun_post_property_str(char *str);
void app_aliyun_register_set_property_cb(aliyun_property_set_cb_t set_property_cb);
void app_aliyun_register_get_property_cb(aliyun_property_get_cb_t get_property_cb);

void app_aliyun_init(aliyun_event_cb_t event_cb);

#endif