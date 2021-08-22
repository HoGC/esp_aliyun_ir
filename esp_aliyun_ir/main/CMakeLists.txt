set(COMPONENT_ADD_INCLUDEDIRS .
                                shell/include
                                ota/include
                                ir_remote/include
                                aliyun/include)

set(COMPONENT_SRCS ./app_main.c
                    ./shell/app_shell.c
                    ./ota/app_ota.c
                    ./ir_remote/app_ir.c
                    ./aliyun/app_aliyun.c )

register_component()
