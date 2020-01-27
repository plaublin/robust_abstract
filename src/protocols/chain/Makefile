include ../../Makefile.common

all: objs

INCLUDES := $(COMMON_INCS) $(INCLUDES)

CPPFLAGS += -UREPLY_BY_PRIMARY


C_FILES=\
C_Client.cc           C_Replica.cc        C_Message.cc\
C_Reply.cc 				C_Mes_queue.cc 		C_Principal.cc\
C_Request.cc		  C_Node.cc					C_Checkpoint.cc \
C_ITimer.cc 		C_Time.cc 				C_Rep_info.cc	\
chain_libbyz.cc	C_Panic.cc C_Abort.cc C_Abort_certificate.cc\
C_Smasher.cc C_Missing.cc C_Get_a_grip.cc C_Client_notification.cc \
Blocking_circular_buffer.cc


c_FILES=

H_FILES := $(C_FILES:%.C=%.H)

h_FILES := $(c_FILES:%.c=%.h)

OBJ_FILES:=$(C_FILES:%.cc=%.o) $(c_FILES:%.c=%.o)

objs: $(OBJ_FILES)

clean: common-clean
clobber: common-clobber

include ../../Makefile.common.tail
