TARGET   = snapperbench
SRC_CC   = main.cc 
LIBS    += base vfs snap

INC_DIR += $(REP_DIR)/include

# Optional payload number
ifdef PAYLOAD_NUM
	CC_OPT += -DPAYLOAD_NUM=$(PAYLOAD_NUM)
else
	CC_OPT += -DPAYLOAD_NUM=100
endif

# Optional payload size
ifdef PAYLOAD_SIZE
	CC_OPT += -DPAYLOAD_SIZE=$(PAYLOAD_SIZE)
else
	CC_OPT += -DPAYLOAD_SIZE=1
endif
