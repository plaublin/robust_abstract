#ifndef _C_Message_tags_h
#define _C_Message_tags_h 1

//
// Each message type is identified by one of the tags in the set below.
//

const short C_Request_tag=1;
const short C_Reply_tag=2;
const short C_Checkpoint_tag=3;
const short C_Deliver_tag=4;
const short C_Panic_tag=5;
const short C_Abort_tag=6;
const short C_Missing_tag=7;
const short C_Get_a_grip_tag=8;
const short C_Client_notification_tag=9;

#endif // _C_Message_tags_h
