#include <stdlib.h>
#include "th_assert.h"
#include "A_Message.h"
#include "A_Node.h"

A_Message::A_Message(unsigned sz) :
  msg(0), max_size(sz)
{
  if (sz != 0)
  {
    msg = (A_Message_rep*) malloc(max_size);
    msg->tag = -1;
    msg->size = 0;
    msg->extra = 0;
    msg->mac_is_valid = 1;
  }
}

A_Message::A_Message(int t, unsigned sz)
{
  max_size = sz;
  msg = (A_Message_rep*) malloc(max_size);
  msg->tag = t;
  msg->size = max_size;
  msg->extra = 0;
  msg->mac_is_valid = 1;
}

A_Message::A_Message(A_Message_rep *cont)
{
  msg = cont;
  max_size = -1; // To prevent contents from being deallocated or trimmed
}

A_Message::~A_Message()
{
  if (max_size > 0)
    free((void*) msg);
}

void A_Message::trim()
{
}

void A_Message::set_size(int size)
{
  th_assert(msg, "Invalid state");
  int aligned = size;
  for (int i = size; i < aligned; i++)
    ((char*) msg)[i] = 0;
  msg->size = aligned;
}

bool A_Message::convert(char *src, unsigned len, int t, int sz, A_Message &m)
{
  // First check if src is large enough to hold a A_Message_rep
  if (len < sizeof(A_Message_rep))
    return false;

  // Check alignment.

  // Next check tag and message size
  A_Message ret((A_Message_rep*) src);
  if (!ret.has_tag(t, sz))
    return false;

  m = ret;
  return true;
}

bool A_Message::encode(FILE* o)
{
  int csize = size();

  size_t sz = fwrite(&max_size, sizeof(int), 1, o);
  sz += fwrite(&csize, sizeof(int), 1, o);
  sz += fwrite(msg, 1, csize, o);

  return sz == 2U + csize;
}

bool A_Message::decode(FILE* i)
{
  delete msg;

  size_t sz = fread(&max_size, sizeof(int), 1, i);
  msg = (A_Message_rep*) malloc(max_size);

  int csize;
  sz += fread(&csize, sizeof(int), 1, i);

  if (msg == 0 || csize < 0 || csize > max_size)
    return false;

  sz += fread(msg, 1, csize, i);
  return sz == 2U + csize;
}

void A_Message::init()
{
}

const char *A_Message::stag()
{
  static char *string_tags[] =
  { "Free_message", "Request", "Reply", "Pre_prepare", "Prepare", "Commit",
      "Checkpoint", "Status", "View_change", "New_view", "View_change_ack",
      "New_key", "Meta_data", "Meta_data_d", "Data_tag", "Fetch" };
  return string_tags[tag()];
}
