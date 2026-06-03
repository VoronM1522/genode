TARGET   = snappertests
SRC_CC   = main.cc 
LIBS    += base vfs snap

INC_DIR += $(REP_DIR)/include

ifdef TESTS
	CC_OPT += -DTESTS=$(TESTS)
else
	CC_OPT += -DTESTS=1000
endif
