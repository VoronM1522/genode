TARGET  := check_init_flag
INC_DIR += $(PRG_DIR)/../include
INC_DIR += $(call select_from_repositories,/src/lib/tresor/include)
SRC_CC  += main.cc
LIBS    += base vfs
