#include "A_View_info.h"
#include "A_View_change.h"
#include "A_View_change_ack.h"
#include "A_New_view.h"
#include "A_Pre_prepare.h"
#include "A_State.h"
#include "A_Status.h"
#include "A_Replica.h"
#include "A_Big_req_table.h"
#include "th_assert.h"
#include "K_max.h"
#include "Log.t"
#include "Array.h"

A_View_info::VCA_info::VCA_info() :
  v(0), vacks((A_View_change_ack *) 0, A_node->n())
{
}

void A_View_info::VCA_info::clear()
{
  for (int i = 0; i < A_node->n(); i++)
  {
    delete vacks[i];
    vacks[i] = 0;
  }
  v = 0;
}

A_View_info::A_View_info(int ident, View vi) :
  v(vi), id(ident), last_stable(0), oplog(max_out), last_views((View) 0,
      A_node->n()), last_vcs((A_View_change*) 0, A_node->n()), my_vacks(
      (A_View_change_ack*) 0, A_node->n()), vacks(A_node->n()), last_nvs(A_node->n())
{
  vacks._enlarge_by(A_node->n());
  last_nvs._enlarge_by(A_node->n());
}

A_View_info::~A_View_info()
{
  for (int i = 0; i < last_vcs.size(); i++)
  {
    delete last_vcs[i];
    delete my_vacks[i];
  }
}

void A_View_info::add_complete(A_Pre_prepare* pp)
{
  th_assert(pp->view() == v, "Invalid argument");

  OReq_info &ri = oplog.fetch(pp->seqno());
  th_assert(pp->view() >= 0 && pp->view() > ri.v, "Invalid argument");

  ri.clear();
  ri.v = pp->view();
  ri.lv = ri.v;
  ri.d = pp->digest();
  ri.m = pp;
}

void A_View_info::add_incomplete(Seqno n, Digest const &d)
{
  OReq_info &ri = oplog.fetch(n);

  if (ri.d == d)
  {
    // A_Message matches the one in the log
    if (ri.m != 0)
    {
      // Logged message was prepared
      ri.lv = v;
    }
    else
    {
      ri.v = v;
    }
  }
  else
  {
    // A_Message is different from the one in log
    if (ri.m != 0)
    {
      delete ri.m;
      ri.m = 0;
    }
    else
    {
      ri.lv = ri.v;
    }

    // Remember last f()+2 digests.
    View minv = View_max;
    int mini = 0;
    for (int i = 0; i < A_node->f() + 2; i++)
    {
      if (ri.ods[i].d == ri.d)
      {
        ri.ods[i].v = ri.lv;
        mini = -1;
        break;
      }

      if (ri.ods[i].v < minv)
      {
        mini = i;
        minv = ri.ods[i].v;
      }
    }

    if (mini >= 0)
    {
      ri.ods[mini].d = ri.d;
      ri.ods[mini].v = ri.lv;
    }

    ri.d = d;
    ri.v = v;
  }
}

void A_View_info::send_proofs(Seqno n, View vi, int dest)
{
  if (oplog.within_range(n))
  {
    OReq_info &ri = oplog.fetch(n);
    A_Principal* p = A_node->i_to_p(dest);

    for (int i = 0; i < A_node->f() + 2; i++)
    {
      if (ri.ods[i].v >= vi)
      {
        A_Prepare prep(ri.ods[i].v, n, ri.ods[i].d, p);
        //  A_node->send(&prep, dest);
        A_replica->send(&prep, dest);
      }
    }
  }
}

A_Pre_prepare* A_View_info::pre_prepare(Seqno n, Digest& d)
{
  if (oplog.within_range(n))
  {
    OReq_info& ri = oplog.fetch(n);
    if (ri.m && ri.d == d)
    {
      th_assert(ri.m->digest() == ri.d && ri.m->seqno() == n, "Invalid state");
      return ri.m;
    }
  }

  return 0;
}

A_Pre_prepare* A_View_info::pre_prepare(Seqno n, View v)
{
  if (oplog.within_range(n))
  {
    OReq_info& ri = oplog.fetch(n);
    if (ri.m && ri.v >= v)
    {
      th_assert(ri.m->seqno() == n, "Invalid state");
      return ri.m;
    }
  }

  return 0;
}

bool A_View_info::prepare(Seqno n, Digest& d)
{
  // Effects: Returns true iff "this" logs that this A_replica sent a
  // prepare with digest "d" for sequence number "n".

  if (oplog.within_range(n))
  {
    OReq_info &ri = oplog.fetch(n);

    if (ri.d == d)
      return true;

    for (int i = 0; i < A_node->f() + 2; i++)
    {
      if (ri.ods[i].d == d)
        return true;
    }
  }

  return false;
}

void A_View_info::discard_old()
{
  // Discard view-changes, view-change acks, and new views with view
  // less than "v"
  for (int i = 0; i < A_node->n(); i++)
  {
    if (last_vcs[i] && last_vcs[i]->view() < v)
    {
      delete last_vcs[i];
      last_vcs[i] = 0;
    }

    delete my_vacks[i];
    my_vacks[i] = 0;

    if (vacks[i].v < v)
    {
      vacks[i].clear();
      vacks[i].v = v;
    }

    if (last_nvs[i].view() < v)
    {
      last_nvs[i].clear();
    }
  }
}

void A_View_info::view_change(View vi, Seqno last_executed, A_State *state)
{
  v = vi;

  discard_old();

  // Create my view-change message for "v".
  A_View_change *vc = new A_View_change(v, last_stable, id);

  // Add checkpoint information to the message.
  for (Seqno i = last_stable; i <= last_executed; i += checkpoint_interval)
  {
    Digest dc;
    if (state->digest(i, dc))
      vc->add_checkpoint(i, dc);
  }

  A_Big_req_table* brt = A_replica->big_reqs();

  // Add request information to the message.
  for (Seqno i = last_stable + 1; i <= last_stable + max_out; i++)
  {
    OReq_info &ri = oplog.fetch(i);

    // Null requests are not added to message.
    if (ri.v >= 0)
    {
      vc->add_request(i, ri.v, ri.lv, ri.d, ri.m != 0);

      if (ri.m)
      {
        // Update A_replica's brt to prevent discarding of big requests
        // referenced by logged pre-prepares.
        for (int j = 0; j < ri.m->num_big_reqs(); j++)
          brt->add_pre_prepare(ri.m->big_req_digest(j), j, i, v);
      }
    }
  }

  // Discard stale big reqs.
  brt->view_change(v);

  int size_of_vc = sizeof(A_View_change_rep) + sizeof(Req_info)
      * vc->rep().n_reqs;

  /*
   printf(
   "A_Replica %i, primary %i, view %qd, checking view change for view %qd (seqno=%qd) of size %i\n",
   A_replica->id(), A_replica->primary(), A_replica->view(), vi,
   vc->last_stable(), size_of_vc);
   char *toto = vc->contents();
   for (int i = 0; i < size_of_vc/4; i++)
   {
   fprintf(stderr, " %x", *(toto + (4 * i)));
   }
   fprintf(stderr, "--] END OF MESSAGE\n");
   */

  /*
   printf(
   "BEFORE AUTHENTICATION: A_Replica %i, primary %i, view %qd, checking view change for view %qd (seqno=%qd) of size %i\n",
   A_replica->id(), A_replica->primary(), A_replica->view(), vi,
   vc->last_stable(), size_of_vc);
   // char *toto = vc->contents() + size_of_vc;
   //for (int i = 0; i < A_node->auth_size() / 4; i++)
   char *toto = vc->contents();
   for (int i = 0; i < vc->size() / 4; i++)
   {
   fprintf(stderr, " %x", *(toto + (4 * i)));
   }
   fprintf(stderr, "--] END OF MESSAGE\n");
   */

  int nb_auth = 1;
  for (int n = 0; n < nb_auth; n++)
  {
    vc->re_authenticate();
    /*
     fprintf(
     stderr,
     "AFTER AUTHENTICATION %i: A_Replica %i, primary %i, view %qd, checking view change for view %qd (seqno=%qd) of size %i\n",
     n, A_replica->id(), A_replica->primary(), A_replica->view(), vi,
     vc->last_stable(), size_of_vc);
     char *toto = vc->contents();
     for (int i = 0; i < vc->size() / 4; i++)
     {
     fprintf(stderr, " %x", *(toto + (4 * i)));
     }
     fprintf(stderr, "--] END OF MESSAGE\n");*/
  }

  vc_sent = A_currentTime();
  //fprintf(stderr, "VI: sending view change\n");
  //  A_node->send(vc, A_Node::All_replicas);

  /*
   fprintf(stderr, "A_Replica %i, primary %i, view %i: sending a view change\n",
   A_replica->id(), A_replica->primary(), A_replica->view());
   */

  A_replica->send(vc, A_Node::All_replicas);

  // Record that this message was sent.
  last_vcs[id] = vc;
  last_views[id] = v;

  int primv = A_node->primary(v);
  if (primv != id)
  {
    // If we are not the primary, send view-change acks for messages in
    // last_vcs with view v.
    for (int i = 0; i < A_node->n(); i++)
    {
      A_View_change *lvc = last_vcs[i];
      if (lvc && lvc->view() == v && i != id && i != primv)
      {
        A_View_change_ack *vack = new A_View_change_ack(v, id, i, lvc->digest());
        my_vacks[i] = vack;
        //  A_node->send(vack, primv);

#ifdef MSG_DEBUG
        fprintf(
            stderr,
            "Replica %i, primary %i, view %qd, sending a view change ack to %i for view %qd, acknowledging %i (1)\n",
            A_replica->id(), A_replica->primary(), A_replica->view(), primv,
            vack->view(), vack->vc_id());
#endif

        A_replica->send(vack, primv);
      }
    }
  }
  else
  {
    // If we are the primary create new view info for "v"
    A_NV_info &n = last_nvs[id];
    th_assert(n.view() <= v, "Invalid state");

    // Create an empty new-view message and add it to "n". Information
    // will later be added to "n/nv".
    A_New_view* nv = new A_New_view(v);
    n.add(nv, this);

    // Move any view-change messages for view "v" to "n".
    for (int i = 0; i < A_node->n(); i++)
    {
      A_View_change* vc = last_vcs[i];
      if (vc && vc->view() == v && n.add(vc, true))
      {
        last_vcs[i] = 0;
      }
    }

    // Move any view-change acks for messages in "n" to "n"
    for (int i = 0; i < A_node->n(); i++)
    {
      VCA_info& vaci = vacks[i];
      if (vaci.v == v)
      {
        for (int j = 0; j < A_node->n(); j++)
        {
          if (vaci.vacks[j] && n.add(vaci.vacks[j]))
            vaci.vacks[j] = 0;
        }
      }
    }
  }
}

bool A_View_info::add(A_View_change* vc)
{
  //A_replica id
  int vci = vc->id();

  //id of the new view
  int vcv = vc->view();

  //if the new view id is old: delete the view change
  if (vcv < v)
  {
    delete vc;

    /*
     fprintf(
     stderr,
     "[1]A_Replica %i (primary=%i) (view %qd) handles a View change, adding View Change message failed because %i < %qd\n",
     A_node->id(), A_node->primary(), A_node->view(), vcv, v);
     */

    return false;
  }

  // Try to match vc with a new-view message.
  A_NV_info &n = last_nvs[A_node->primary(vcv)];
  bool stored = false;
  int primv = A_node->primary(v);
  if (n.view() == vcv)
  {
    // There is a new-view message corresponding to "vc"
    bool verified = vc->verify();

    /*
     if (!verified && A_node->primary())
     {
     fprintf(
     stderr,
     "WARNING !!!!!!!!!!!!!!: I am the primary and the verification of the view change from %i has failed \n",
     vc->id());
     }
     */

    stored = n.add(vc, verified);
    fflush(NULL);

    if (stored && id == primv && vcv == v)
    {
      // Try to add any buffered view-change acks that match vc to "n"
      for (int i = 0; i < A_node->n(); i++)
      {
        VCA_info& vaci = vacks[i];
        if (vaci.v == v && vaci.vacks[vci] && n.add(vaci.vacks[vci]))
        {
          vaci.vacks[vci] = 0;
        }
      }
    }
    else
    {
      /*
       if (!stored)
       {
       fprintf(
       stderr,
       "[1]A_Replica %i (primary=%i) (view %qd) handles a View change, adding View Change message failed because add returned false\n",
       A_node->id(), A_node->primary(), A_node->view());
       }

       fprintf(
       stderr,
       "[1]A_Replica %i (primary=%i) (view %qd) handles a View change, id=%i, primv=%i, vcv=%i, v=%qd\n",
       A_node->id(), A_node->primary(), A_node->view(), id, primv, vcv, v);
       */
    }

    if (verified && vcv > last_views[vci])
    {
      last_views[vci] = vcv;
    }
  }
  else
  {
    // There is no matching new-view.
    if (vcv > last_views[vci] && vc->verify())
    {
      delete last_vcs[vci];
      last_vcs[vci] = vc;
      last_views[vci] = vcv;
      stored = true;

      if (id != primv && vci != primv && vcv == v)
      {
        // Send view-change ack.
        A_View_change_ack *vack = new A_View_change_ack(v, id, vci, vc->digest());
        th_assert(my_vacks[vci] == 0, "Invalid state");

        my_vacks[vci] = vack;
        //  A_node->send(vack, primv);

#ifdef MSG_DEBUG
        fprintf(
            stderr,
            "Replica %i, primary %i, view %qd, sending a view change ack to %i for view %qd, acknowledging %i (2)\n",
            A_replica->id(), A_replica->primary(), A_replica->view(), primv,
            vack->view(), vack->vc_id());
#endif

        A_replica->send(vack, primv);
      }
    }
    else
    {
      /*
       printf(
       "[1]A_Replica %i (primary=%i) (view %qd) handles a View change, adding View Change message failed because %i <= %qd or !verified()\n",
       A_node->id(), A_node->primary(), A_node->view(), vcv, last_views[vci]);
       */
    }
  }

  if (!stored)
    delete vc;

  return stored;
}

void A_View_info::add(A_New_view* nv)
{
  int nvi = nv->id();
  int nvv = nv->view();

  if (nvv >= v && nv->verify())
  {
    A_NV_info &n = last_nvs[nvi];
    if (nv->view() > n.view())
    {
      bool stored = n.add(nv, this);
      if (stored)
      {
        // Move any view-change messages for view "nvv" to "n".
        for (int i = 0; i < last_vcs.size(); i++)
        {
          A_View_change* vc = last_vcs[i];
          if (vc && vc->view() == nvv && n.add(vc, true))
          {
            last_vcs[i] = 0;
          }
        }
      }
      else
      {
        /*
         fprintf(stderr, "A_Replica %i, view %qd, stored is false\n",
         A_replica->id(), A_replica->view());
         */
      }

      return;
    }
    else
    {
      /*
       fprintf(stderr, "A_Replica %i, view %qd, adding new view failed (1)\n",
       A_replica->id(), A_replica->view());
       */
    }
  }
  else
  {
    /*
     fprintf(stderr, "A_Replica %i, view %qd, adding new view failed (2)\n",
     A_replica->id(), A_replica->view());
     */
  }

  delete nv;
}

void A_View_info::add(A_View_change_ack* vca)
{
  int vci = vca->vc_id();
  int vcv = vca->view();

  if (vca->verify())
  {
    int primvcv = A_node->primary(vcv);

    A_NV_info &n = last_nvs[primvcv];
    if (n.view() == vcv && n.add(vca))
    {
      /*
       fprintf(
       stderr,
       "A_Replica %i, primary %i, view %qd. There is a new-view message corresponding to vca (id %i, view %i)\n",
       A_replica->id(), A_replica->primary(), A_replica->view(), vci, vcv);
       */

      // There is a new-view message corresponding to "vca"
      return;
    }

    if (id == primvcv)
    {
      VCA_info &vcai = vacks[vca->id()];
      if (vcai.v <= vcv)
      {
        if (vcai.v < vcv)
        {
          vcai.clear();
        }

        /*
         fprintf(
         stderr,
         "A_Replica %i, primary %i, view %qd. Adding the vca (id %i, view %i)\n",
         A_replica->id(), A_replica->primary(), A_replica->view(), vci, vcv);
         */

        delete vcai.vacks[vci];
        vcai.vacks[vci] = vca;
        vcai.v = v;
        return;
      }
      else
      {
        /*
         fprintf(
         stderr,
         "A_Replica %i, primary %i, view %qd. vca (id %i, view %i), vcai.v=%i, vcv=%i\n",
         A_replica->id(), A_replica->primary(), A_replica->view(), vci, vcv,
         vcai.v, vcv);
         */
      }
    }
    else
    {
      /*
       fprintf(
       stderr,
       "A_Replica %i, primary %i, view %qd. I am not the primary for vca (id %i, view %i)\n",
       A_replica->id(), A_replica->primary(), A_replica->view(), vci, vcv);
       */
    }
  }
  else
  {
    /*
     fprintf(
     stderr,
     "A_Replica %i, primary %i, view %qd. Verify is false for vca (id %i, view %i)\n",
     A_replica->id(), A_replica->primary(), A_replica->view(), vci, vcv);
     */
  }

  delete vca;
}

inline View A_View_info::k_max(int k) const
{
  return K_max<View> (k, last_views.as_pointer(), A_node->n(), View_max);
}

View A_View_info::max_view() const
{
  View ret = k_max(A_node->f() + 1);
  return ret;
}

View A_View_info::max_maj_view() const
{
  View ret = k_max(A_node->n_f());
  return ret;
}

void A_View_info::set_received_vcs(A_Status *m)
{
  th_assert(m->view() == v, "Invalid argument");

  A_NV_info& nvi = last_nvs[A_node->primary(v)];
  if (nvi.view() == v)
  {
    // There is a new-view message for the current view.
    nvi.set_received_vcs(m);
  }
  else
  {
    for (int i = 0; i < A_node->n(); i++)
    {
      if (last_vcs[i] != 0 && last_vcs[i]->view() == v)
      {
        m->mark_vcs(i);
      }
    }
  }
}

void A_View_info::set_missing_pps(A_Status *m)
{
  th_assert(m->view() == view(), "Invalid argument");

  if (last_nvs[A_node->primary(view())].new_view())
    last_nvs[A_node->primary(view())].set_missing_pps(m);
}

A_View_change *A_View_info::my_view_change(A_Time **t)
{
  A_View_change *myvc;
  if (last_vcs[id] == 0)
  {
    myvc = last_nvs[A_node->primary(v)].view_change(id);
  }
  else
  {
    myvc = last_vcs[id];
  }
  if (t && myvc)
    *t = &vc_sent;
  return myvc;
}

A_New_view *A_View_info::my_new_view(A_Time **t)
{
  return last_nvs[id].new_view(t);
}

void A_View_info::mark_stable(Seqno ls)
{
  last_stable = ls;
  oplog.truncate(last_stable + 1);

  last_nvs[A_node->primary(v)].mark_stable(ls);
}

void A_View_info::clear()
{
  oplog.clear(last_stable + 1);

  for (int i = 0; i < A_node->n(); i++)
  {
    delete last_vcs[i];
    last_vcs[i] = 0;
    last_views[i] = v;
    vacks[i].clear();

    delete my_vacks[i];
    my_vacks[i] = 0;

    last_nvs[i].clear();
  }
  vc_sent = A_zeroTime();
}

bool A_View_info::shutdown(FILE* o)
{
  int wb = 0, ab = 0;
  bool ret = true;
  for (Seqno i = last_stable + 1; i <= last_stable + max_out; i++)
  {
    OReq_info& ori = oplog.fetch(i);

    wb += fwrite(&ori.lv, sizeof(View), 1, o);
    wb += fwrite(&ori.v, sizeof(View), 1, o);
    wb += fwrite(&ori.d, sizeof(Digest), 1, o);

    bool hasm = ori.m != 0;
    wb += fwrite(&hasm, sizeof(bool), 1, o);
    ab += 4;

    if (hasm)
      ret &= ori.m->encode(o);
  }

  bool is_complete = has_new_view(v);
  wb += fwrite(&is_complete, sizeof(bool), 1, o);
  ab++;

  return ret & (ab == wb);
}

bool A_View_info::restart(FILE* in, View rv, Seqno ls, bool corrupt)
{
  v = rv;
  last_stable = ls;
  id = A_node->id();

  for (int i = 0; i < A_node->n(); i++)
    last_views[i] = v;

  vc_sent = A_zeroTime();

  oplog.clear(last_stable + 1);

  bool ret = !corrupt;
  int rb = 0, ab = 0;
  if (!corrupt)
  {
    for (Seqno i = last_stable + 1; i <= last_stable + max_out; i++)
    {
      OReq_info& ori = oplog.fetch(i);

      rb += fread(&ori.lv, sizeof(View), 1, in);
      rb += fread(&ori.v, sizeof(View), 1, in);
      rb += fread(&ori.d, sizeof(Digest), 1, in);

      bool hasm;
      rb += fread(&hasm, sizeof(bool), 1, in);
      delete ori.m;
      if (hasm)
      {
        ori.m = (A_Pre_prepare*) new A_Message;
        ret &= ori.m->decode(in);
      }
      else
      {
        ori.m = 0;
      }

      // Check invariants
      if (hasm)
      {
        ret &= (ori.m->digest() == ori.d) & (ori.m->view() == ori.v) & (ori.lv
            >= ori.v);
      }
      else
      {
        ret &= (ori.lv < ori.v || ori.v == -1);
      }

      if (!ret)
      {
        ori = OReq_info();
        return false;
      }

      ab += 4;
    }
  }

  bool is_complete;
  rb += fread(&is_complete, sizeof(bool), 1, in);
  ab++;
  if (is_complete)
    last_nvs[A_node->primary(v)].make_complete(v);

  return ret & (ab == rb);
}

bool A_View_info::enforce_bound(Seqno b, Seqno ks, bool corrupt)
{
  if (corrupt || last_stable > b - max_out)
  {
    last_stable = ks;
    oplog.clear(ks + 1);
    return false;
  }

  for (Seqno i = b; i <= last_stable + max_out; i++)
  {
    OReq_info& ori = oplog.fetch(i);
    if (ori.v >= 0)
    {
      oplog.clear(ks + 1);
      return false;
    }
  }

  return true;
}

void A_View_info::mark_stale()
{
  for (int i = 0; i < A_node->n(); i++)
  {
    if (i != id)
    {
      delete last_vcs[i];
      last_vcs[i] = 0;
      if (last_views[i] >= v)
        last_views[i] = v;
    }

    delete my_vacks[i];
    my_vacks[i] = 0;

    A_View_change *vc = last_nvs[i].mark_stale(id);
    if (vc && vc->view() == view())
    {
      last_vcs[id] = vc;
    }
    else
    {
      delete vc;
    }

    vacks[i].clear();
  }
}

