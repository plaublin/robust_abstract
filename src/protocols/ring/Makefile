include ../../Makefile.common

all: objs

INCLUDES := $(COMMON_INCS) $(INCLUDES)

# REPLY_BY_ENTRY: Receipient of the message responds to the client. Exec only when ACK is received.
#DEFINES := $(DEFINES) -UREPLY_BY_ENTRY -DRECOVERY -UUSE_MACS -DTRACES -UUSE_CLIENT_SIGNATURES -UUSE_CHECKPOINTS -UDO_TIMINGS -UDO_TIMINGS_TICKS
DEFINES := $(DEFINES) -UREPLY_BY_ENTRY -DRECOVERY -UUSE_MACS -UUSE_CLIENT_SIGNATURES -UUSE_CHECKPOINTS -UDO_TIMINGS -UDO_TIMINGS_TICKS

#CPPFLAGS:= $(INCS) -g -Wall $(DEFINES) 

C_FILES=\
SuperFastHash.cc \
R_ITimer.cc \
R_Client.cc           R_Replica.cc        R_Message.cc\
R_BaseRequest.cc \
R_Reply.cc 				R_Mes_queue.cc 		R_Principal.cc\
R_Request.cc		  R_ACK.cc \
R_Node.cc\
R_Checkpoint.cc \
R_Rep_info.cc		  ring_libbyz.cc \
R_Time.cc R_Request_history.cc \
umacv.cc


c_FILES=

H_FILES := $(C_FILES:%.C=%.H)

h_FILES := $(c_FILES:%.c=%.h)

OBJ_FILES:=$(C_FILES:%.cc=%.o) $(c_FILES:%.c=%.o)

objs: $(OBJ_FILES) .maindep

.maindep: $(OBJ_FILES) Makefile
	echo $(CURDIR): $(addprefix $(CURDIR)/,$^) > .maindep

clean: common-clean
clobber: common-clobber

include ../../Makefile.common.tail
