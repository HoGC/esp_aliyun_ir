set(COMPONENT_ADD_INCLUDEDIRS ./ir_decoder/src/include)

set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS ./ir_decoder/src/ir_ac_apply.c
                   ./ir_decoder/src/ir_ac_binary_parse.c
                   ./ir_decoder/src/ir_ac_build_frame.c
                   ./ir_decoder/src/ir_ac_control.c
                   ./ir_decoder/src/ir_ac_parse_forbidden_info.c
                   ./ir_decoder/src/ir_ac_parse_frame_info.c
                   ./ir_decoder/src/ir_ac_parse_parameter.c
                   ./ir_decoder/src/ir_decode.c
                   ./ir_decoder/src/ir_tv_control.c
                   ./ir_decoder/src/ir_utils.c)

register_component()