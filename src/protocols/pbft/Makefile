include ../../Makefile.common

all: objs

C_FILES=

c_FILES= fail.c umac.c

H_FILES := $(C_FILES:%.C=%.H)

h_FILES := $(c_FILES:%.c=%.h)

OBJ_FILES:=$(C_FILES:%.cc=%.o) $(c_FILES:%.c=%.o)

objs: $(OBJ_FILES)

clean: common-clean
	-cd replica && $(MAKE) clean
	-cd client && $(MAKE) clean

clobber: common-clean
	-cd replica && $(MAKE) clobber
	-cd client && $(MAKE) clobber

include ../../Makefile.common.tail

