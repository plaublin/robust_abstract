include ../../Makefile.common

all: objs

INCLUDES := $(COMMON_INCS) $(INCLUDES)

C_FILES=\
O_Client.cc               O_Replica.cc \
O_Reply.cc 					O_Message.cc\
O_Request.cc            O_Node.cc \
O_Rep_info.cc         	O_Principal.cc \
O_Checkpoint.cc  O_Panic.cc O_Abort.cc \
O_Missing.cc O_Get_a_grip.cc \
O_Abort_certificate.cc \
O_Smasher.cc \
O_ITimer.cc O_Time.cc \
cbft_libbyz.cc 


c_FILES= 

H_FILES := $(C_FILES:%.C=%.H)

h_FILES := $(c_FILES:%.c=%.h)

OBJ_FILES:=$(C_FILES:%.cc=%.o) $(c_FILES:%.c=%.o)

objs: $(OBJ_FILES)

clean: common-clean
clobber: common-clobber

include ../../Makefile.common.tail
