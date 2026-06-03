SRC_CC   = snapper.cc backlink.cc archive.cc utils.cc xxhash32.cc
LIBS    += base vfs

INC_DIR += $(REP_DIR)/include

vpath %.cc $(REP_DIR)/src/lib

SHARED_LIB = yes
