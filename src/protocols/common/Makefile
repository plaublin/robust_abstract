include ../../Makefile.common

all: objs

INCS:=-I$(TOP)/common

#CPPFLAGS:= $(INCS) -g -Wall -DRECOVERY 
#CPPFLAGS:= $(INCS) -g -ggdb -Wall -DRECOVERY -Wno-non-template-friend
#CPPFLAGS:= $(INCS) -O9 -funroll-loops -ffast-math -malign-double -march=pentiumpro -fomit-frame-pointer -fno-exceptions -DNDEBUG 
#CPPFLAGS:= $(INCS) -O3 -march=pentiumpro -fomit-frame-pointer -fno-exceptions -DNDEBUG   -DRECOVERY
#CPPFLAGS:= $(INCS) -O3 -march=pentiumpro -fno-exceptions -DNDEBUG   -DRECOVERY -pg
#CPPFLAGS:= $(INCS) -O3 -march=pentiumpro -fno-exceptions -DNDEBUG   -DRECOVERY 

CPPFLAGS := $(INCS) -fno-exceptions $(CPPFLAGS)
CFLAGS   := $(INCS) $(CFLAGS)
LIBS     += -lpthread

C_FILES=\
Digest.cc \
DSum.cc \
Request_history.cc \
measurements.cc \
RobustMonitor.cc

c_FILES= TCP_network.c

H_FILES := $(C_FILES:%.C=%.H)

h_FILES := $(c_FILES:%.c=%.h)

OBJ_FILES:=$(C_FILES:%.cc=%.o) $(c_FILES:%.c=%.o)

objs: $(OBJ_FILES)

clean: common-clean
clobber: common-clobber

include ../../Makefile.common.tail
