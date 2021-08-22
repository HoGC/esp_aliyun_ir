COMPONENT_ADD_INCLUDEDIRS = ./ir_decoder/src/include
# COMPONENT_SRCDIRS = ./ir_decoder/src

COMPONENT_OBJS := ir_decoder/src/ir_ac_apply.o \
				  ir_decoder/src/ir_ac_binary_parse.o \
				  ir_decoder/src/ir_ac_build_frame.o \
				  ir_decoder/src/ir_ac_control.o \
				  ir_decoder/src/ir_ac_parse_forbidden_info.o \
				  ir_decoder/src/ir_ac_parse_frame_info.o \
				  ir_decoder/src/ir_ac_parse_parameter.o \
				  ir_decoder/src/ir_decode.o \
				  ir_decoder/src/ir_tv_control.o \
				  ir_decoder/src/ir_utils.o 

COMPONENT_SRCDIRS := ir_decoder/src