#include <string.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <sys/select.h>

#include "shell.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "driver/uart.h"
#include <lwip/sockets.h>

#include "app_config.h"

static const char* TAG = "app_shell";

#ifdef CONFIG_ENABLE_SHELL

#define     SHELL_UART      UART_NUM_0

Shell shell;
static char shellBuffer[512];

static int uart_fd = -1;
static int tcp_server_fd = -1;
static int tcp_client_fd = -1;


/**
 * @brief 用户shell写
 * 
 * @param data 数据
 */
signed short user_shell_write(char *data, unsigned short len)
{
    signed short uart_len = 0, tcp_len = 0;

    uart_len = uart_write_bytes(SHELL_UART, data, len);

#ifdef CONFIG_ENABLE_TCP_SHELL
    if(tcp_client_fd != -1){
        tcp_len = write(tcp_client_fd, data, len);
    }
#endif
    return (uart_len>tcp_len?uart_len:tcp_len);
}

static void shell_uart_init(){
    uart_config_t uartConfig = {
        .baud_rate = 74880,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(SHELL_UART, &uartConfig);
    uart_driver_install(SHELL_UART, 256 * 2, 0, 0, NULL, 0);

    if ((uart_fd = open("/dev/uart/0", O_RDWR)) == -1) {
        ESP_LOGE(TAG, "Cannot open UART");
        return;
    }
}


#ifdef CONFIG_ENABLE_TCP_SHELL
static void shell_tcp_init(void){

    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));

    if ((tcp_server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        ESP_LOGE(TAG, "Cannot create socket");
        return;
    }

    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8899);

    if (bind(tcp_server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
        ESP_LOGE(TAG, "tsps bind error");
        return;
    }

    if (listen(tcp_server_fd, 1) != 0) {
        ESP_LOGE(TAG, "tsps listen error");
        return;
    }
}
#endif

static void shell_task(void *param)
{
    shell_uart_init();

    shell.write = user_shell_write;
    shell.read = NULL;
    shellInit(&shell, shellBuffer, 512);

    vTaskDelay(1000 / portTICK_RATE_MS);

#ifdef CONFIG_ENABLE_TCP_SHELL
    shell_tcp_init();
#endif

    while(1)
    {
        int s;
        char data;
        int max_fd = -1;
        fd_set shell_rfds;
        struct timeval tv = {
            .tv_sec = 5,
            .tv_usec = 0,
        };

        FD_ZERO(&shell_rfds);
        if(uart_fd != -1){
            FD_SET(uart_fd, &shell_rfds);
        }
        if(tcp_server_fd != -1){
            FD_SET(tcp_server_fd, &shell_rfds);
        }
        if(tcp_client_fd != -1){
            FD_SET(tcp_client_fd, &shell_rfds);
        }

        max_fd = (uart_fd>tcp_server_fd)?uart_fd:tcp_server_fd;
        if(tcp_client_fd > max_fd){
            max_fd = tcp_client_fd;
        }

        if(max_fd == -1){
            vTaskDelete(NULL);
        }

        s = select(max_fd + 1, &shell_rfds, NULL, NULL, &tv);
        if (s < 0) {
            ESP_LOGE(TAG, "Select failed: errno %d", errno);
            break;
        } else if (s == 0) {
            continue;
        } else {
            if (FD_ISSET(tcp_server_fd, &shell_rfds)) {
                int fd = -1;
                struct sockaddr_in client_addr;
				socklen_t client_addr_size = sizeof(client_addr);
                fd = accept(tcp_server_fd, (struct sockaddr *)&client_addr, &client_addr_size);
                if(fd < 0){
                    continue;
                }
                if(tcp_client_fd != -1){
                    close(fd);
                    continue;
                }
                tcp_client_fd = fd;
                ESP_LOGI(TAG, "client connect: %d", tcp_client_fd);
            }else if(FD_ISSET(uart_fd, &shell_rfds)){
                if (read(uart_fd, &data, 1) > 0) {
                    // ESP_LOGI(TAG, "Received: %c", buf);
                    shellHandler(&shell, data);
                }
            }else if(FD_ISSET(tcp_client_fd, &shell_rfds)){
                if (read(tcp_client_fd, &data, 1) > 0) {
                    // ESP_LOGI(TAG, "Received: %c", buf);
                    shellHandler(&shell, data);
                }else{
                    ESP_LOGI(TAG, "client disconnect: %d", tcp_client_fd);
                    close(tcp_client_fd);
                    tcp_client_fd = -1;
                }
            }
        }
    }
}



/**
 * @brief 用户shell初始化
 * 
 */
void app_shell_init(void)
{
    xTaskCreate(shell_task, "shell_task", 2048, NULL, 4, NULL);
}

void shell_printf(const char *fmt, ...){
    
    int ret;
    va_list va;
    char *pbuf;
    
#if SHELL_SUPPORT_END_LINE == 1

    if(shell.write){
        va_start(va, fmt);
        ret = vasprintf(&pbuf, fmt, va);
        if (ret < 0)
            return;
        shellWriteEndLine(&shell, pbuf, strlen(pbuf));
        va_end(va);
        free(pbuf);
    }else
#endif
    {
        va_start(va, fmt);
        ret = vasprintf(&pbuf, fmt, va);
        printf(pbuf);
        va_end(va);
        free(pbuf);
    }
}

void shell_hexdump(const char* buf, int len)
{

    const int width = 16;

	if (len < 1 || buf == NULL) return;
 
	const char *hexChars = "0123456789ABCDEF";
	int i = 0;
	char c = 0x00;
	char str_print_able[width+1];
	char str_hex_buffer[width * 3 + 1];

	for (i = 0; i < (len / width) * width; i += width)
	{
		int j = 0;
		for (j = 0; j < width; j++)
		{
			c = buf[i + j];
 
			// hex
			int z = j * 3;
			str_hex_buffer[z++] = hexChars[(c >> 4) & 0x0F];
			str_hex_buffer[z++] = hexChars[c & 0x0F];
			// str_hex_buffer[z++] = (j < 10 && !((j + 1) % 8)) ? '_' : ' ';
			str_hex_buffer[z++] = ' ';
 
			// string with space repalced
			if (c < 32 || c == '\0' || c == '\t' || c == '\r' || c == '\n' || c == '\b')
				str_print_able[j] = '.';
			else
				str_print_able[j] = c;
		}
		str_hex_buffer[width * 3] = 0x00;
		str_print_able[j] = 0x00;

        shell_printf(" %04x %02d   %s %s  \n", i, width, str_hex_buffer, str_print_able);
	}
 
	// 处理剩下的不够16字节长度的部分
	int leftSize = len % width;
	if (leftSize < 1) return;
	int j = 0;
	int pos = i;
	for (; i < len; i++)
	{
		c = buf[i];
 
		// hex
		int z = j * 3;
		str_hex_buffer[z++] = hexChars[(c >> 4) & 0x0F];
		str_hex_buffer[z++] = hexChars[c & 0x0F];
		str_hex_buffer[z++] = ' ';
 
		// string with space repalced
		if (c < 32 || c == '\0' || c == '\t' || c == '\r' || c == '\n' || c == '\b')
			str_print_able[j] = '.';
		else
			str_print_able[j] = c;
		j++;
	}
	str_hex_buffer[leftSize * 3] = 0x00;
	str_print_able[j] = 0x00;
 
	for (j = leftSize; j < width; j++)
	{
		int z = j * 3;
		str_hex_buffer[z++] = ' ';
		str_hex_buffer[z++] = ' ';
		str_hex_buffer[z++] = ' ';
	}
	str_hex_buffer[width * 3] = 0x00;
    shell_printf(" %04x %02d   %s %s  \n", pos, leftSize, str_hex_buffer, str_print_able);
}


#if SHELL_SUPPORT_END_LINE == 1
int esp_log_write_str(const char *s){

    static int cnt = 0;
    static char log_str[512] = {0};

    if(shell.write){
        if(s[0] == '\033'){
            strcpy(log_str, s);
        }else{
            strcat(log_str, s);
            cnt++;
            if(cnt == 2){
                if(log_str[0] == '\033'){
                    strcat(log_str, "\033[0m");
                }
                strcat(log_str, "\r\n");
                shellWriteEndLine(&shell, log_str, strlen(log_str));
                memset(log_str, 0 ,512);
                cnt = 0;
            }
        }
        return 0;
    }else{
        return printf("%s", s);
    }
}
#endif

#endif
