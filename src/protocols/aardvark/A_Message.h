#ifndef _A_Message_h
#define _A_Message_h 1

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "th_assert.h"
#include "types.h"
#include "A_Message_tags.h"

// bool ALIGNED(void *ptr) or bool ALIGNED(long sz)
// // Effects: Returns true iff the argument is aligned to ALIGNMENT
// #define ALIGNED(ptr) (((long)(ptr))%ALIGNMENT == 0)
//
// // int ALIGNED_SIZE(int sz)
// // Effects: Increases sz to the least multiple of ALIGNMENT greater 
// // than size.
// #define ALIGNED_SIZE(sz) ((ALIGNED(sz)) ? (sz) : (sz)-(sz)%ALIGNMENT+ALIGNMENT)
//


// Maximum message size. Must verify ALIGNED_SIZE.
const int A_Max_message_size = 9000;

//
// All messages have the following format:
// 
struct A_Message_rep
{
  short tag;
  short extra; // May be used to store extra information.
  //  char unused[1000];
  int size; // Must be a multiple of 8 bytes to ensure proper
  // alignment of embedded messages.
  int mac_is_valid; // 1 if the mac is valid, 0 otherwise
  // since we have a problem with mac generation/verification, the
  // verification returns this value instead.

  //Followed by char payload[size-sizeof(A_Message_rep)];
};

class A_Message
{
  //
  // Generic messages
  //
public:
  A_Message(unsigned sz = 0);
  // Effects: Creates an untagged A_Message object that can hold up
  // to "sz" bytes and holds zero bytes. Useful to create message
  // buffers to receive messages from the network.

  A_Message(int t, unsigned sz);
  // Effects: Creates a message with tag "t" that can hold up to "sz"
  // bytes. Useful to create messages to send to the network.

  ~A_Message();
  // Effects: Deallocates all storage associated with this message.

  void trim();
  // Effects: Deallocates surplus storage.

  char* contents();
  // Effects: Return a byte string with the message contents.  
  // TODO: should be protected here because of request iterator in
  // A_Pre_prepare.cc

  int size() const;
  // Effects: Fetches the message size.

  int tag() const;
  // Effects: Fetches the message tag.

  bool has_tag(int t, int sz) const;
  // Effects: If message has tag "t", its size is greater than "sz",
  // its size less than or equal to "max_size", and its size is a
  // multiple of ALIGNMENT, returns true.  Otherwise, returns false.


  View view() const;
  // Effects: Returns any view associated with the message or 0.

  bool full() const;
  // Effects: Messages may be full or empty. Empty messages are just
  // digests of full messages. 
  /*
   // A_Message-specific heap management operators.
   void *operator new(size_t s);
   void operator delete(void *x, size_t s);
   */

  static void init();
  // Effects: Should be called once to initialize the memory allocator.
  // Before any message is allocated.

  const char *stag();
  // Effects: Returns a string with tag name

  bool encode(FILE* o);
  bool decode(FILE* i);
  // Effects: Encodes and decodes object state from stream. Return
  // true if successful and false otherwise.

  static void debug_alloc()
  { /*if (a) a->debug_print();*/
  }
  // Effects: Prints debug information for memory allocator.

  void check_msg();
  // cheks that msg is not null and msg is aligned


  // return true if the mac is valid, false otherwise
  static bool is_mac_valid(A_Message_rep *msg)
  {
    return (msg->mac_is_valid == 1 ? true : false);
  }

  // set msg->mac_is_valid to an unvalid value (0)
  static void set_mac_unvalid(A_Message_rep *msg)
  {
    msg->mac_is_valid = 0;
  }

  // set msg->mac_is_valid to an valid value (1)
  static void set_mac_valid(A_Message_rep *msg)
  {
    msg->mac_is_valid = 1;
  }

protected:
  A_Message(A_Message_rep *contents);
  // Requires: "contents" contains a valid A_Message_rep.
  // Effects: Creates a message from "contents". No copy is made of
  // "contents" and the storage associated with "contents" is not
  // deallocated if the message is later deleted. Useful to create 
  // messages from reps contained in other messages.

  void set_size(int size);
  // Effects: Sets message size to the smallest multiple of 8 bytes
  // greater than equal to "size" and pads the message with zeros
  // between "size" and the new message size. Important to ensure
  // proper alignment of embedded messages.

  int msize() const;
  // Effects: Fetches the maximum number of bytes that can be stored in
  // this message.

  static bool convert(char *src, unsigned len, int t, int sz, A_Message &m);
  // Requires: convert can safely read up to "len" bytes starting at
  // "src" Effects: If "src" is a A_Message_rep for which "has_tag(t,
  // sz)" returns true and sets m to have contents "src". Otherwise,
  // it returns false.  No copy is made of "src" and the storage
  // associated with "contents" is not deallocated if "m" is later
  // deleted.


  friend class A_Node;
  friend class A_Replica;
  friend class A_Verifier_thread;
  friend class A_Pre_prepare;

  A_Message_rep *msg; // Pointer to the contents of the message.
  int max_size; // Maximum number of bytes that can be stored in "msg"
  // or "-1" if this instance is not responsible for
  // deallocating the storage in msg.
  // Invariant: max_size <= 0 || 0 < msg->size <= max_size 

private:
  //
  // A_Message-specific memory management
  //
  //static Log_allocator *a;
};

// Methods inlined for speed

inline int A_Message::size() const
{
  return msg->size;
}

inline int A_Message::tag() const
{
  return msg->tag;
}

inline bool A_Message::has_tag(int t, int sz) const
{

  if (!msg || msg->tag != t || msg->size < sz)
    return false;

  if (max_size >= 0 && msg->size > max_size)
    return false;

  return true;
}

inline View A_Message::view() const
{
  return 0;
}

inline bool A_Message::full() const
{
  return true;
}

inline int A_Message::msize() const
{
  return (max_size >= 0) ? max_size : msg->size;
}

inline char *A_Message::contents()
{
  return (char *) msg;
}

// check if the message is not null and different from a magic number
// meaning "deleted msg"
inline void A_Message::check_msg()
{
  th_assert(msg,"null msg");
  th_assert((int)msg != 0x386592a7,"deleted msg");
}

#endif //_A_Message_h
