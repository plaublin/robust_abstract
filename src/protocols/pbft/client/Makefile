include ../../../Makefile.common

all: objs

DEFINES += -UPBFT_DO_SWITCHING

INCLUDES := -I../ $(COMMON_INCS) $(INCLUDES)


C_FILES=\
PBFT_C_Client.cc             PBFT_C_Replica.cc            PBFT_C_New_key.cc\
PBFT_C_Commit.cc             PBFT_C_Message.cc            PBFT_C_Reply.cc \
PBFT_C_Node.cc               PBFT_C_Request.cc \
PBFT_C_Checkpoint.cc         PBFT_C_Pre_prepare.cc        PBFT_C_Req_queue.cc \
PBFT_C_Prepare.cc            PBFT_C_Status.cc             PBFT_C_Prepared_cert.cc\
PBFT_C_Principal.cc          PBFT_C_Time.cc 		  PBFT_C_ITimer.cc \
PBFT_C_Meta_data.cc          PBFT_C_Data.cc               PBFT_C_Fetch.cc \
PBFT_C_Meta_data_cert.cc     PBFT_C_State.cc              PBFT_C_pbft_libbyz.cc \
PBFT_C_View_change.cc        PBFT_C_New_view.cc           PBFT_C_View_change_ack.cc \
PBFT_C_View_info.cc          PBFT_C_NV_info.cc            PBFT_C_Rep_info.cc \
PBFT_C_Meta_data_d.cc        PBFT_C_Query_stable.cc       PBFT_C_Reply_stable.cc \
PBFT_C_Stable_estimator.cc   PBFT_C_Statistics.cc \
PBFT_C_Big_req_table.cc      PBFT_C_Pre_prepare_info.cc		


c_FILES= 

H_FILES := $(C_FILES:%.C=%.H)

h_FILES := $(c_FILES:%.c=%.h)

OBJ_FILES:=$(C_FILES:%.cc=%.o) $(c_FILES:%.c=%.o)

objs: $(OBJ_FILES)

clean: common-clean
clobber: common-clobber

include ../../../Makefile.common.tail
