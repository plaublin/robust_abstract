include ../../Makefile.common

all: objs

INCLUDES := $(COMMON_INCS) $(INCLUDES)
DEFINES := $(DEFINES) #-DCLIENT_MULTICAST


C_FILES=\
zl_Client.cc               zl_Replica.cc \
zl_Reply.cc 					zl_Message.cc\
zl_Mes_queue.cc \
zl_Request.cc            zl_Node.cc \
zl_Rep_info.cc         	zl_Principal.cc \
zl_Checkpoint.cc  zl_Panic.cc zl_Abort.cc \
zl_Missing.cc zl_Get_a_grip.cc zl_Order_request.cc \
zl_Abort_certificate.cc zl_Smasher.cc \
zl_ITimer.cc zl_Time.cc \
zl_Client_notification.cc zlight_libbyz.cc


c_FILES= 

H_FILES := $(C_FILES:%.C=%.H)

h_FILES := $(c_FILES:%.c=%.h)

OBJ_FILES:=$(C_FILES:%.cc=%.o) $(c_FILES:%.c=%.o)

objs: $(OBJ_FILES)

clean: common-clean
clobber: common-clobber

include ../../Makefile.common.tail
