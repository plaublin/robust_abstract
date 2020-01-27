include ../../Makefile.common

all: objs

INCLUDES := $(COMMON_INCS) $(INCLUDES)

C_FILES=\
A_Client.cc  A_Replica.cc  A_New_key.cc\
A_Commit.cc  A_Message.cc  A_Reply.cc \
A_Node.cc  A_Request.cc \
A_Checkpoint.cc  A_Pre_prepare.cc A_Req_queue.cc \
A_Prepare.cc  A_Status.cc  A_Prepared_cert.cc\
A_ITimer.cc  A_Principal.cc  \
A_Meta_data.cc  A_Data.cc  A_Fetch.cc \
A_Meta_data_cert.cc A_State.cc  aardvark_libbyz.cc \
A_View_change.cc A_New_view.cc  A_View_change_ack.cc \
A_View_info.cc  A_NV_info.cc  A_Rep_info.cc \
A_Meta_data_d.cc	 A_Big_req_table.cc\
A_Statistics.cc  A_Time.cc A_Verifier_thread.cc \
A_Pre_prepare_info.cc A_attacks.cc  A_Wrapped_request.cc \
A_Circular_buffer.cc tcp_net.cc


c_FILES= umac.c

H_FILES := $(C_FILES:%.C=%.H)

h_FILES := $(c_FILES:%.c=%.h)

OBJ_FILES:=$(C_FILES:%.cc=%.o) $(c_FILES:%.c=%.o)

objs: $(OBJ_FILES)

clean: common-clean
clobber: common-clobber

include ../../Makefile.common.tail
