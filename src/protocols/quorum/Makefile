include ../../Makefile.common

all: objs

INCLUDES := $(COMMON_INCS) $(INCLUDES)


C_FILES=\
Q_Client.cc               Q_Replica.cc \
Q_Reply.cc 					Q_Message.cc\
Q_Request.cc            Q_Node.cc \
Q_Rep_info.cc         	Q_Principal.cc \
Q_Checkpoint.cc  Q_Panic.cc Q_Abort.cc \
Q_Missing.cc Q_Get_a_grip.cc \
Q_Abort_certificate.cc \
Q_Smasher.cc Q_Mes_queue.cc \
Q_ITimer.cc Q_Time.cc \
quorum_libbyz.cc


c_FILES= 

H_FILES := $(C_FILES:%.C=%.H)

h_FILES := $(c_FILES:%.c=%.h)

OBJ_FILES:=$(C_FILES:%.cc=%.o) $(c_FILES:%.c=%.o)

objs: $(OBJ_FILES)

clean: common-clean
clobber: common-clobber

include ../../Makefile.common.tail
