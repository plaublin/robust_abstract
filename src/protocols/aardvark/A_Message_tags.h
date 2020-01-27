#ifndef _A_Message_tags_h
#define _A_Message_tags_h 1

//
// Each message type is identified by one of the tags in the set below.
//

const short A_Free_message_tag=0; // Used to mark free message reps.
                                // A valid message may never use this tag.
const short A_Request_tag=1;
const short A_Reply_tag=2;
const short A_Pre_prepare_tag=3;
const short A_Prepare_tag=4;
const short A_Commit_tag=5;
const short A_Checkpoint_tag=6;
const short A_Status_tag=7;
const short A_View_change_tag=8;
const short A_New_view_tag=9;
const short A_View_change_ack_tag=10;
const short A_New_key_tag=11;
const short A_Meta_data_tag=12;
const short A_Meta_data_d_tag=13;
const short A_Data_tag=14;
const short A_Fetch_tag=15;
const short A_Wrapped_request_tag=18;

#endif // _A_Message_tags_h
