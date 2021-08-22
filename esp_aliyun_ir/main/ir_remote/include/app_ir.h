#ifndef __APP_IR_H__
#define __APP_IR_H__

#include "stdint.h"

#include "ir_decode.h"

#define remote_status_t     t_remote_ac_status


void app_ir_remote_init(void);
int app_ir_remote_status_send(const char *protocol, int control_code, remote_status_t *status);

#endif
