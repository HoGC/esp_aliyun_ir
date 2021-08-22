set(COMPONENT_ADD_INCLUDEDIRS "letter-shell/src" "port")

set(COMPONENT_SRCS "letter-shell/src/shell.c"
                    "letter-shell/src/shell_cmd_list.c"
                    "letter-shell/src/shell_companion.c"
                    "letter-shell/src/shell_ext.c")

register_component()