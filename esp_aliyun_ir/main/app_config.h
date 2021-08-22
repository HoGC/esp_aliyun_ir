#ifndef _APP_CONFIG_
#define _APP_CONFIG_

#define CONFIG_ENABLE_SHELL                     
#ifdef CONFIG_ENABLE_SHELL
#define CONFIG_ENABLE_TCP_SHELL                    
#endif             


#define CONFIG_LED_GPIO                         16
#define CONFIG_LED_ACTIVE_LEVEL                 0

#define CONFIG_BUTTON_GPIO                      5
#define CONFIG_BUTTON_ACTIVE_LEVEL              0

#endif