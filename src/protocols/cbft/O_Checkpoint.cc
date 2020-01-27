#include <stdlib.h>
#include <strings.h>
#include "th_assert.h"
#include "O_Message_tags.h"
#include "O_Checkpoint.h"
#include "O_Node.h"
#include "O_Principal.h"
#include "MD5.h"

O_Checkpoint::O_Checkpoint() :
	O_Message(O_Checkpoint_tag, sizeof(O_Checkpoint_rep)+O_node->sig_size())
{
	rep().id = O_node->id();
}

O_Checkpoint::~O_Checkpoint()
{
}

void O_Checkpoint::re_authenticate(O_Principal *p, bool stable)
{
  if (rep().extra != 1 && stable) {
    rep().extra = 1;
    O_node->gen_signature(contents(), sizeof(O_Checkpoint_rep), 
			contents()+sizeof(O_Checkpoint_rep));
  }
}

bool O_Checkpoint::verify()
{
  // O_Checkpoints must be sent by O_replicas.
  if (!O_node->is_replica(id())) return false;

  // Check signature size.
  if (size()-(int)sizeof(O_Checkpoint_rep) < O_node->sig_size(id())) 
    return false;

  return O_node->i_to_p(id())->verify_signature(contents(), sizeof(O_Checkpoint_rep),
					      contents()+sizeof(O_Checkpoint_rep));
}

bool O_Checkpoint::convert(O_Message *m1, O_Checkpoint *&m2)
{

	if (!m1->has_tag(O_Checkpoint_tag, sizeof(O_Checkpoint_rep)))
	{
		return false;
	}

	m2 = new O_Checkpoint();
	memcpy(m2->contents(), m1->contents(), m1->size());
	//free(m1->contents());
	// TODO : VIVIEN WE SHOULD NOT COMMENT THIS LINE (THIS CREATES A LIBC ERROR).
	delete m1;
	m2->trim();
	return true;
}
