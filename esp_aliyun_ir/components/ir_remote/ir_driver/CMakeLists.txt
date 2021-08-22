set(COMPONENT_ADD_INCLUDEDIRS .)

set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS ./ir/rx.c
                   ./ir/tx.c
                   ./ir/raw.c
                   ./ir/generic.c)

register_component()