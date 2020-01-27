#include "th_assert.h"
#include "A_Message_tags.h"
#include "A_Meta_data.h"
#include "Partition.h"
#include "A_Node.h"
#include "A_Replica.h"
#include "A_Principal.h"
#include "Bitmap.h"

A_Meta_data::A_Meta_data(Request_id r, int l, int i, Seqno lu, Seqno lm,  Digest& d) 
  : A_Message(A_Meta_data_tag, A_Max_message_size) {
  th_assert(l < PLevels, "Invalid argument");
  th_assert(i < PLevelSize[l], "Invalid argument");
  rep().rid = r;
  rep().lu = lu;
  rep().lm = lm;
  rep().l = l;
  rep().i = i;
  rep().id = A_replica->id();
  rep().d = d;
  rep().np = 0;
  set_size(sizeof(A_Meta_data_rep));
}


void A_Meta_data::add_sub_part(int i, Digest &d) {
  A_Part_info &p = parts()[rep().np];
  p.i = i;
  p.d = d;
  rep().np++;
  set_size(sizeof(A_Meta_data_rep)+rep().np*sizeof(A_Part_info));
}


A_Meta_data::Sub_parts_iter::Sub_parts_iter(A_Meta_data *m) {
  msg = m;
  cur_mod = 0;
  max_mod = m->rep().np;
  if (m->level() != PLevels-1) {
    index = m->index()*PSize[m->level()+1];
    max_index = index+PSize[m->level()+1];
  } else {
    index = m->index();
    max_index = index+1;
  }
}
    

bool A_Meta_data::Sub_parts_iter::get(int& i, Digest& d) {
  if (index < max_index) {
    if (cur_mod < max_mod) {
      A_Part_info *cur = msg->parts() + cur_mod;
      if (index < cur->i) {
	d.zero();
	i = index;
	index++;
	return true;
      }

      th_assert(index ==  cur->i, "Invalid state");
      d = cur->d;
      i = index;
      index++;
      cur_mod++;
      return true;
    }

    d.zero();
    i = index;
    index++;
    return true;    
  }
  return false;
}


bool A_Meta_data::verify() {
  // Meta-data must be sent by replicas.
  if (!A_node->is_replica(id()) || A_node->id() == id()) 
    return false;

  // A_Meta_data messages are not sent for leaf partitions
  if (level() < 0 || level() >= PLevels-1)
    return false;

  if (index() < 0 || index() >=  PLevelSize[level()])
    return false;

  if (rep().np > 1 && rep().np >  PSize[level()+1])
    return false;

  if (last_mod() > last_uptodate())
    return false;

  // Check sizes
  int old_size = sizeof(A_Meta_data_rep) + rep().np*sizeof(A_Part_info);
  if (size() < old_size) {
    return false;
  }

  A_Part_info *ps = parts();
  int min = index()*PSize[level()+1];
  int max = min+PSize[level()+1];
  Bitmap sparts(max-min);
  for (int i=0; i < rep().np; i++) {
    int si = ps[i].i;
    if (si < min || si >= max || sparts.test(si-min)) {
      return false;
    }
    sparts.set(si-min);
  }
  
  return true;
}


bool A_Meta_data::convert(A_Message *m1, A_Meta_data  *&m2) {
  if (!m1->has_tag(A_Meta_data_tag, sizeof(A_Meta_data_rep)))
    return false;
  m1->trim();
  m2 = (A_Meta_data*)m1;
  return true;
}
