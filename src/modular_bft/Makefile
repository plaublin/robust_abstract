include ../Makefile.common

all: objs

CPPFLAGS += $(PROTO_INCS)
CFLAGS   += $(PROTO_INCS)

%.o:: %.cc
	$(CC) $(CPPFLAGS)  -o $@ -c $<

%.o:: %.c
	$(C) $(CFLAGS) -o $@ -c $<

libmodular_BFT.o: libmodular_BFT_choice.h

C_FILES=\
libmodular_BFT.cc

c_FILES= 

H_FILES := $(C_FILES:%.C=%.H)

h_FILES := $(c_FILES:%.c=%.h)

OBJ_FILES:=$(C_FILES:%.cc=%.o) $(c_FILES:%.c=%.o)

objs: $(OBJ_FILES)

clean: common-clean
clobber: common-clobber
	-rm -f libmodular_BFT.a

include ../Makefile.common.tail
