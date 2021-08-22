#ifndef _APP_SHELL_H_
#define _APP_SHELL_H_

#include "shell.h"

extern Shell shell;

void app_shell_init(void);

void shell_printf(const char *fmt, ...);
void shell_hexdump(const char* buf, int len);
#endif
