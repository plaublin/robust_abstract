#include <string.h>
#include "th_assert.h"
#include "A_Message_tags.h"
#include "A_Data.h"

A_Data::A_Data(int i, Seqno lm, char *data)
: A_Message(A_Data_tag, sizeof(A_Data_rep)) {
  rep().index = i;
  rep().padding = 0;
  rep().lm = lm;

  // TODO: Avoid this copy using sendmsg with iovecs.
  memcpy(rep().data, data, Block_size);                 

} 

bool A_Data::convert(A_Message *m1, A_Data  *&m2) {
  if (!m1->has_tag(A_Data_tag, sizeof(A_Data_rep)))
    return false;

  m2 = (A_Data*)m1;
  m2->trim();
  return true;
}
