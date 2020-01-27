#include "A_NV_info.h"
#include "A_Replica.h"
#include "A_View_change.h"
#include "A_View_change_ack.h"
#include "A_New_view.h"
#include "A_View_info.h"
#include "A_Pre_prepare.h"
#include "A_Status.h"
#include "Array.h"
#include "K_max.h"
#include "A_parameters.h"

//
// A_NV_info::VC_info methods:
//

A_NV_info::VC_info::VC_info() :
  vc(0), ack_count(0), ack_reps(A_node->n()), req_sum(false)
{
}

A_NV_info::VC_info::~VC_info()
{
  delete vc;
}

void A_NV_info::VC_info::clear()
{
  delete vc;
  vc = 0;
  ack_count = 0;
  ack_reps.clear();
  req_sum = false;
}

A_NV_info::Req_sum::Req_sum() :
  r_pproofs(A_node->n())
{
}

A_NV_info::Req_sum::~Req_sum()
{
  pi.zero();
}
// This is necessary to prevent any pre-prepare in pi from being
// deallocated. Because Req_sum assignment only performs a shallow
// copy.

//
// A_NV_info methods:
//

A_NV_info::A_NV_info() :
  v(0), nv(0), vc_target(0), vc_cur(0), vcs(A_node->n())
{
  vcs._enlarge_by(A_node->n());
  chosen_ckpt = -1;
  max = -1;
  base = -1;
  n_complete = 0;
  is_complete = false;
  nv_sent = A_zeroTime();
}

A_NV_info::~A_NV_info()
{
  clear();
}

void A_NV_info::clear()
{
  v = 0;
  delete nv;
  nv = 0;
  vc_target = 0;

  for (int i = 0; i < vcs.size(); i++)
  {
    vcs[i].clear();
  }
  vc_cur = 0;

  ckpts.clear();
  chosen_ckpt = -1;
  max = -1;
  base = -1;
  min = -1;
  nv_sent = A_zeroTime();
  for (int i = 0; i < reqs.size(); i++)
  {
    for (int j = 0; j < reqs[i].size(); j++)
    {
      reqs[i][j].pi.clear();
    }
    reqs[i].clear();
  }
  reqs.clear();
  comp_reqs.clear();

  n_complete = 0;
  is_complete = false;
}

void A_NV_info::make_complete(View vi)
{
  v = vi;
  is_complete = true;
}

A_View_change* A_NV_info::mark_stale(int id)
{
  A_View_change *pres = 0;
  if (!is_complete)
  {
    pres = vcs[id].vc;
    vcs[id].vc = 0;
    View ov = v;

    clear();

    if (ov > 0 && A_node->primary(ov) == id)
    {
      // The primary recreates its state to allow the construction of
      // a complete new-view for this view.
      A_New_view* nv = new A_New_view(ov);
      add(nv, vi);

      th_assert(pres != 0, "Invalid state");
      add(pres, true);
      pres = 0;
    }
  }

  return pres;
}

bool A_NV_info::add(A_New_view* m, A_View_info* parent)
{
  th_assert(m->verify() || A_node->id() == A_node->primary(m->view()), "Invalid argument");
  th_assert(parent != 0, "Invalid argument");

  if (m->view() <= v)
  {
    delete m;
    return false;
  }

  // Remove any old information.
  if (v != 0)
    clear();

  // Add m to this.
  v = m->view();
  nv = m;
  vi = parent;

  // Set vc_target.
  for (int i = 0; i < A_node->n(); i++)
  {
    Digest vd;
    if (m->view_change(i, vd))
      vc_target++;
  }

  return true;
}

bool A_NV_info::add(A_View_change *m, bool verified)
{
  th_assert(m->view() == v, "Invalid argument");

  int vcid = m->id();
  if (vcs[vcid].vc != 0 || is_complete)
  {
    /*
     printf(
     "[2]adding View Change message failed with vcid=%i and is_complete=%s\n",
     vcid, (is_complete ? "true" : "false"));
     */

    if (vcs[vcid].vc != 0)
    {
      /*
       A_View_change* m2 = vcs[vcid].vc;
       printf(
       "[2]there is already a A_View_change: id=%i, view=%qd, last_stable=%qd, max_seqno=%qd <-> id=%i, view=%qd, last_stable=%qd, max_seqno=%qd\n",
       m2->id(), m2->view(), m2->last_stable(), m2->max_seqno(), m->id(),
       m->view(), m->last_stable(), m->max_seqno());
       */
    }

    return false;
  }

  bool is_primary = A_node->primary(v) == A_node->id();

  if (!is_primary)
  {
    Digest d;
    bool a = !nv->view_change(vcid, d);
    bool b = true;
    if (!a)
    {
      b = (d != m->digest());
    }

    if (a || b)
    {
      /*
       fprintf(stderr,
       "[2]adding View Change message failed with a=%s and b=%s\n",
       (a ? "true" : "false"), (b ? "true" : "false"));
       */
      return false;
    }
  }

  if (!verified)
  {
    if (is_primary || !m->verify_digest() || vcs[vcid].ack_count < A_node->f())
    {
      /*
       printf(
       "[2]adding View Change message failed with is_primary=%s, vcs[%i].ack_count=%i and A_node->f()=%i\n",
       (is_primary ? "true" : "false"), vcid, vcs[vcid].ack_count, A_node->f());
       */
      return false;
    }
  }

  vcs[vcid].vc = m;
  vc_cur++;

  if (is_primary && m->id() == A_node->id())
  {
    nv->add_view_change(m->id(), m->digest());
    th_assert(!is_complete,"first\n");
    summarize(m);
  }

  if (!is_primary && vc_cur == vc_target)
  {
    // We have all the needed view-change messages. Check if they
    // form a valid new-view.
    if (!check_new_view())
    {
      // Primary is faulty.      
      fprintf(stderr, "Primary %d is faulty\n", A_node->primary(v));
    }
  }

  return true;
}

bool A_NV_info::add(A_View_change_ack* m)
{
  th_assert(m->verify() && m->view() == v, "Invalid argument");

  int vci = m->vc_id();
  int mid = m->id();

  bool is_primary = A_node->primary(v) == A_node->id();

  if (is_complete)
  {
    /*
     fprintf(stderr, "A_Replica %i, primary %i, view %qd. is_complete is true.\n",
     A_replica->id(), A_replica->primary(), A_replica->view());
     */
    return false;
  }

  Digest d;
  bool in_nv = nv->view_change(vci, d);

  if (!is_primary)
  {
    if (!in_nv)
    {
      /*
       fprintf(stderr,
       "A_Replica %i, primary %i, view %qd. is_primary = in_nv = false.\n",
       A_replica->id(), A_replica->primary(), A_replica->view());
       */
      return false;
    }
  }
  else
  {
    if (!vcs[vci].vc)
    {
      /*
       fprintf(stderr,
       "A_Replica %i, primary %i, view %qd. vcs[vci].vc = false.\n",
       A_replica->id(), A_replica->primary(), A_replica->view());
       */
      return false;
    }

    d = vcs[vci].vc->digest();
  }

  if (!in_nv && m->vc_digest() == d && !vcs[vci].ack_reps.test(mid))
  {
    vcs[vci].ack_reps.set(mid);
    vcs[vci].ack_count++;

    if (vcs[vci].ack_count == A_node->f() * 2 - 1 && A_node->primary(v)
        == A_node->id())
    {
      // This view change has enough acks: add it to the new-view.
      A_View_change *vc = vcs[vci].vc;
      nv->add_view_change(vci, vc->digest());
      th_assert(!is_complete,"second\n");
      summarize(vc);
    }
  }
  delete m;

  /*
   fprintf(stderr, "A_Replica %i, primary %i, view %qd. Adding vca succesful\n",
   A_replica->id(), A_replica->primary(), A_replica->view());
   */

  return true;
}

void A_NV_info::summarize(A_View_change *vc)
{
  th_assert(!is_complete, "Invalid state");

  int size = ckpts.size();
  bool was_chosen = chosen_ckpt >= 0;
  bool match = false;
  int n_le = 0;
  Seqno max_seqno = vc->max_seqno();

  Digest vclc;
  Seqno vcn = vc->last_stable();
  vc->ckpt(vcn, vclc); // vclc is null if vc has no checkpoint digest

  for (int i = 0; i < size; i++)
  {
    Ckpt_sum& cur = ckpts[i];

    if (cur.n == vcn && cur.d == vclc)
    {
      match = true;
      cur.n_proofs++;
      cur.n_le++;
      if (vc->max_seqno() > cur.max_seqno)
        cur.max_seqno = vc->max_seqno();
    }
    else
    {
      Digest d;
      if (vc->ckpt(cur.n, d) && d == cur.d)
      {
        cur.n_proofs++;
      }

      if (cur.n > vcn)
      {
        cur.n_le++;
        if (vc->max_seqno() > cur.max_seqno)
          cur.max_seqno = vc->max_seqno();
      }
      else if (cur.n < vcn)
      {
        n_le++;
        if (cur.max_seqno > max_seqno)
          max_seqno = cur.max_seqno;
      }
    }

    if (cur.n_proofs >= A_node->f() + 1 && cur.n_le >= A_node->n_f())
    {
      /*
       fprintf(
       stderr,
       "A_Replica %i, primary %i, view %qd. Calling choose_ckpt(%i) from summarize() (1)\n",
       A_replica->id(), A_replica->primary(), A_replica->view(), i);
       */

      choose_ckpt(i);
    }
  }

  if (!match && !vclc.is_zero())
  {
    // vc has checkpoints and no entry matches its last checkpoint: add a new one.
    Ckpt_sum ns;
    ns.n = vcn;
    ns.d = vclc;
    ns.n_le = n_le + 1;
    ns.max_seqno = max_seqno;
    ns.id = vc->id();
    ns.n_proofs = 0;

    // Search view-changes in new-view for proofs
    Digest d;
    for (int i = 0; i < vcs.size(); i++)
    {
      if (nv->view_change(i) && vcs[i].vc->ckpt(vcn, d) && d == vclc)
      {
        ns.n_proofs++;
      }
    }

    ckpts.append(ns);

    if (ns.n_proofs >= A_node->f() + 1 && ns.n_le >= A_node->n_f())
    {
      /*
       fprintf(
       stderr,
       "A_Replica %i, primary %i, view %qd. Calling choose_ckpt(%i) from summarize() (2)\n",
       A_replica->id(), A_replica->primary(), A_replica->view(), ckpts.size() - 1);
       */

      choose_ckpt(ckpts.size() - 1);
    }
  }

  if (was_chosen && !is_complete)
  {
    /*
     fprintf(
     stderr,
     "A_Replica %i, primary %i, view %qd. Calling summarize_reqs() from summarize()\n",
     A_replica->id(), A_replica->primary(), A_replica->view());
     */

    summarize_reqs(vc);
    A_replica->send_status();
  }
}

void A_NV_info::choose_ckpt(int index)
{
  th_assert(A_node->primary(v) == A_node->id(), "Invalid state");
  th_assert(index >= 0 && index < ckpts.size(), "Out of bounds");

  Ckpt_sum& cur = ckpts[index];
  th_assert(cur.n_proofs >= A_node->f()+1 && cur.n_le >= A_node->n_f(), "Invalid argument");

  if (chosen_ckpt < 0)
  {
    chosen_ckpt = index;
    min = cur.n;
    base = cur.n + 1;
    max = cur.max_seqno + 1;
    reqs._enlarge_by(max - base);
    comp_reqs._enlarge_by(max - base);

    for (int i = 0; i < comp_reqs.size(); i++)
    {
      comp_reqs[i] = -1;
    }

    // Summarize requests for all view-change messages in new-view.
    for (int i = 0; i < vcs.size(); i++)
    {
      if (nv->view_change(i))
      {
        /*
         fprintf(
         stderr,
         "A_Replica %i, primary %i, view %qd. Calling summarize_reqs() from choose_ckpt(%i)\n",
         A_replica->id(), A_replica->primary(), A_replica->view(), index);
         */

        summarize_reqs(vcs[i].vc);

        if (is_complete)
        {
          /*
           fprintf(
           stderr,
           "A_Replica %i, primary %i, view %qd. is_complete is true in choose_ckpt(%i)\n",
           A_replica->id(), A_replica->primary(), A_replica->view(), index);
           */
          return;
        }
      }
    }

    /*
     fprintf(
     stderr,
     "A_Replica %i, primary %i, view %qd. choose_ckpt(%i) We loop the loop but there was nothing complete\n",
     A_replica->id(), A_replica->primary(), A_replica->view(), index);
     */
  }
  else if (ckpts[chosen_ckpt].n < cur.n)
  {
    /*
     fprintf(
     stderr,
     "A_Replica %i, primary %i, view %qd. choose_ckpt(%i) BEFORE: n_complete = %i\n",
     A_replica->id(), A_replica->primary(), A_replica->view(), index, n_complete);
     */

    // Adjust n_complete to account for change of range.
    for (Seqno i = min + 1; i <= cur.n; i++)
    {
      if (comp_reqs[i - base] >= 0)
        n_complete--;
    }

    chosen_ckpt = index;
    min = cur.n;
    if (cur.max_seqno + 1 < max)
    {
      max = cur.max_seqno + 1;
    }

    /*
     fprintf(
     stderr,
     "A_Replica %i, primary %i, view %qd. choose_ckpt(%i) AFTER: n_complete = %i\n",
     A_replica->id(), A_replica->primary(), A_replica->view(), index, n_complete);
     */
  }
  else
  {
    /*
     fprintf(
     stderr,
     "A_Replica %i, primary %i, view %qd. choose_ckpt(%i) chosen_ckpt = %i, ckpts[].n = %qd, cur.n = %qd\n",
     A_replica->id(), A_replica->primary(), A_replica->view(), index, chosen_ckpt,
     ckpts[chosen_ckpt].n, cur.n);
     */
  }

  if (n_complete == max - min - 1)
  {
    is_complete = true;

    /*
     fprintf(
     stderr,
     "A_Replica %i, primary %i, view %qd. Calling make_new_view() from choose_ckpt(%i) with n_complete = %i, max = %qd, min = %qd\n",
     A_replica->id(), A_replica->primary(), A_replica->view(), index, n_complete,
     max, min);
     */

    make_new_view();
  }
  else
  {
    /*
     fprintf(
     stderr,
     "A_Replica %i, primary %i, view %qd. Not complete in choose_ckpt(%i): n_complete = %i, max = %qd, min = %qd, sub = %qd or %i\n",
     A_replica->id(), A_replica->primary(), A_replica->view(), index, n_complete,
     max, min, (max - min - 1), (int) (max - min - 1));
     */
  }
}

void A_NV_info::check_comp(Req_sum& cur, Seqno i, int j)
{
  th_assert(!is_complete, "Invalid state");

#ifdef N_LE_GT_N_F_1
  if (comp_reqs[i - base] < 0 && cur.n_proofs >= A_node->f() + 1 && cur.n_le
      >= A_node->n_f() - 1)
#else
  //original code
  if (comp_reqs[i - base] < 0 && cur.n_proofs >= A_node->f() + 1 && cur.n_le
      >= A_node->n_f())
#endif
  {
    /*
     fprintf(
     stderr,
     "A_Replica %i, primary %i, view %qd. check_comp() test succeed: i=%qd, base=%qd, comp_reqs[]=%i, cur.n_proofs=%i, f=%i, cur.n_le=%i, n_f=%i\n",
     A_replica->id(), A_replica->primary(), A_replica->view(), i, base,
     comp_reqs[i - base], cur.n_proofs, A_node->f(), cur.n_le, A_node->n_f());
     */

    if (!cur.pi.is_complete())
    {
      if (cur.pi.pre_prepare() == 0)
      {
        // Check if the missing pre-prepare is in the log.
        A_Pre_prepare* opp = vi->pre_prepare(i, cur.d);

        if (opp)
        {
          cur.pi.add(opp->clone(v));
          cur.n_pproofs = A_node->n();
        }
      }
    }

    if (cur.n_pproofs <= A_node->f() && vi->prepare(i, cur.d))
    {
      // If A_node sent a prepare for this digest in the past, we do not
      // need more positive proofs.
      cur.n_pproofs = A_node->n();
    }

    if (cur.v < 0 || (cur.pi.is_complete() && cur.n_pproofs > A_node->f()))
    {
      comp_reqs[i - base] = j;
      n_complete++;

      /*
       fprintf(stderr,
       "A_Replica %i, primary %i, view %qd. check_comp(), n_complete++: %i\n",
       A_replica->id(), A_replica->primary(), A_replica->view(), n_complete);
       */
    }
    else
    {
      /*
       fprintf(stderr,
       "A_Replica %i, primary %i, view %qd. calling return in check_comp()\n",
       A_replica->id(), A_replica->primary(), A_replica->view());
       */
      return;
    }
  }
  else
  {
    /*
     fprintf(
     stderr,
     "A_Replica %i, primary %i, view %qd. check_comp() test failed: i=%qd, base=%qd, comp_reqs[]=%i, cur.n_proofs=%i, f=%i, cur.n_le=%i, n_f=%i\n",
     A_replica->id(), A_replica->primary(), A_replica->view(), i, base,
     comp_reqs[i - base], cur.n_proofs, A_node->f(), cur.n_le, A_node->n_f());
     */
  }

  /*
   fprintf(
   stderr,
   "A_Replica %i, primary %i, view %qd. check_comp(), min=%qd, max=%qd, n_complete=%i\n",
   A_replica->id(), A_replica->primary(), A_replica->view(), min, max, n_complete);
   */

  // If we gathered enough information, make the new-view message.
  if (n_complete == max - min - 1)
  {
    is_complete = true;

    if (A_replica->primary(v) == A_replica->id())
    {
      /*
       fprintf(
       stderr,
       "A_Replica %i, primary %i, view %qd. calling make_new_view() in check_comp()\n",
       A_replica->id(), A_replica->primary(), A_replica->view());
       */
      make_new_view();
    }
    else
    {
      // Update backups's state to reflect the new view.
      Digest d;
      A_View_change* vc = vcs[nv->which_picked(nv->min())].vc;
      Seqno n = vc->last_stable();
      vc->ckpt(n, d);
      th_assert(!d.is_zero(), "Invalid state");
      Seqno ks = known_stable();

      /*
       fprintf(
       stderr,
       "A_Replica %i, primary %i, view %qd. calling process_new_view() in check_comp()\n",
       A_replica->id(), A_replica->primary(), A_replica->view());
       */

      A_replica->process_new_view(n, d, nv->max(), ks);
    }
  }
}

Seqno A_NV_info::known_stable()
{
  th_assert(is_complete, "Invalid state");

  Seqno* maxs = new Seqno[A_node->n()];

  for (int i = 0; i < A_node->n(); i++)
    maxs[i] = (vcs[i].vc != 0) ? vcs[i].vc->last_stable() : 0;

  Seqno max_stable1 = K_max(A_node->f() + 1, maxs, A_node->n(), Seqno_max);
  th_assert(max_stable1 <= min, "Invalid state");

  for (int i = 0; i < A_node->n(); i++)
  {
    Digest d;
    Seqno n;
    if (vcs[i].vc && vcs[i].vc->last_ckpt(d, n))
    {
      maxs[i] = n;
    }
    else
    {
      maxs[i] = 0;
    }
  }

  Seqno max_stable2 = K_max(A_node->n_f(), maxs, A_node->n(), Seqno_max);
  // TODO: should compute min differently so that I pick the
  // checkpoint (regardless of whether it is claimed stable) with
  // highest sequence number that has enough proofs. This would ensure:
  // max_stable2 <= min
  if (max_stable2 > min)
    max_stable2 = min;

  delete[] maxs;

  return (max_stable1 > max_stable2) ? max_stable1 : max_stable2;
}

void A_NV_info::get_proofs(Req_sum& cur, A_View_change *vc, Seqno i)
{
  bool prepared;
  View v, lv;
  Digest d;
  if (!vc->proofs(i, v, lv, d, prepared))
  {
    if (i > vc->last_stable() && cur.v < 0)
      cur.n_proofs++;
    return;
  }

  if (prepared)
  {
    if (lv >= cur.v & d == cur.d)
    {
      cur.n_proofs++;
      cur.n_pproofs++;
      cur.r_pproofs.set(vc->id());
    }
  }
  else
  {
    if (v >= cur.v & d == cur.d)
    {
      cur.n_proofs++;
      cur.n_pproofs++;
      cur.r_pproofs.set(vc->id());
    }
    else if (cur.v <= lv)
    {
      cur.n_proofs++;
    }
  }
}

void A_NV_info::summarize_reqs(A_View_change *vc)
{
  th_assert(vc != 0 && nv->view_change(vc->id()), "Invalid argument");
  th_assert(!vcs[vc->id()].req_sum, "Invalid argument");
  th_assert(!is_complete, "Invalid state");

  vcs[vc->id()].req_sum = true;

  /*
   fprintf(
   stderr,
   "A_Replica %i, primary %i, view %qd. summarize_reqs() max = %qd, min = %qd, last_stable = %qd\n",
   A_replica->id(), A_replica->primary(), A_replica->view(), max, min,
   vc->last_stable());
   */

  Seqno i = (min > vc->last_stable()) ? min : vc->last_stable();
  for (i = i + 1; i < max; i++)
  {
    Array<Req_sum>& reqsi = reqs[i - base];
    bool match = false;
    int n_le = 0;

    Digest rd;
    View rv = vc->req(i, rd);

    for (int j = 0; j < reqsi.size(); j++)
    {
      Req_sum& cur = reqsi[j];

      if (cur.v == rv && cur.d == rd)
      {
        match = true;
        cur.n_proofs++;
        cur.n_pproofs++;
        cur.r_pproofs.set(vc->id());
        cur.n_le++;
      }
      else
      {
        // Update cur.n_proofs
        get_proofs(cur, vc, i);

        // Update cur.n_le
        if (cur.v > rv)
        {
          cur.n_le++;
        }
        else if (cur.v < rv)
        {
          n_le++;
        }
      }

      /*
       fprintf(
       stderr,
       "A_Replica %i, primary %i, view %qd. summarize_reqs() calling check_comp(cur, %qd, %i) (1)\n",
       A_replica->id(), A_replica->primary(), A_replica->view(), i, j);
       */

      check_comp(cur, i, j);

      if (is_complete)
      {
        /*
         fprintf(
         stderr,
         "A_Replica %i, primary %i, view %qd. summarize_reqs(), is_complete is true\n",
         A_replica->id(), A_replica->primary(), A_replica->view());
         */

        return;
      }
    }

    if (!match)
    {
      // No entry matches this request: add a new one.
      reqsi._enlarge_by(1);
      Req_sum& cur = reqsi.high();
      cur.v = rv;
      cur.d = rd;
      cur.n_le = n_le + 1;
      cur.pi.zero();
      cur.id = vc->id();
      cur.n_proofs = 0;
      cur.n_pproofs = 0;
      cur.r_pproofs.clear();

      // Search view-changes for proofs
      for (int j = 0; j < vcs.size(); j++)
      {
        if (vcs[j].req_sum)
          get_proofs(cur, vcs[j].vc, i);
      }

      /*
       fprintf(
       stderr,
       "A_Replica %i, primary %i, view %qd. summarize_reqs() calling check_comp(cur, %qd, %i) (2)\n",
       A_replica->id(), A_replica->primary(), A_replica->view(), i, reqsi.size()
       - 1);
       */

      check_comp(cur, i, reqsi.size() - 1);
    }
    else
    {
      /*
       fprintf(
       stderr,
       "A_Replica %i, primary %i, view %qd. summarize_reqs() There is a match\n",
       A_replica->id(), A_replica->primary(), A_replica->view());
       */
    }
  }
}

void A_NV_info::make_new_view()
{
  th_assert(A_node->primary(v) == A_node->id(), "Invalid state");
  th_assert(is_complete, "Invalid state");
#ifdef USE_GETTIMEOFDAY
  th_assert((nv_sent.tv_sec == 0) && (nv_sent.tv_usec == 0), "Invalid state");
#else
  th_assert(nv_sent == A_zeroTime(), "Invalid state");
#endif
  nv->set_min(min);

  // Pick the checkpoint
  nv->pick(ckpts[chosen_ckpt].id, min);

  // Pick the requests.
  for (Seqno i = min + 1; i < max; i++)
  {
    th_assert(comp_reqs[i-base] >= 0, "Invalid state");
    Req_sum& cur = reqs[i - base][comp_reqs[i - base]];

    th_assert(cur.pi.is_complete() || cur.v == -1, "Invalid state");
    nv->pick(cur.id, i);
  }

  nv->set_max(max);
  nv->re_authenticate();

  nv_sent = A_currentTime();

  // Update A_replica's state to reflect new-view.
  Seqno ks = known_stable();

  /*
   fprintf(
   stderr,
   "A_Replica %i, primary %i, view %qd. calling process_new_view() in make_new_view()\n",
   A_replica->id(), A_replica->primary(), A_replica->view());
   */
  A_replica->process_new_view(min, ckpts[chosen_ckpt].d, nv->max(), ks);
}

bool A_NV_info::check_new_view()
{
  th_assert(A_node->primary(v) != A_node->id(), "Invalid state");

  // Check chosen checkpoint.
  int cid = nv->which_picked(nv->min());
  A_View_change* vc = vcs[cid].vc;
  min = vc->last_stable();

  if (min != nv->min())
  {
    /*
     fprintf(
     stderr,
     "A_Replica %i, primary %i, view %qd, check_new_view(). min = %qd, nv->min = %qd\n",
     A_replica->id(), A_replica->primary(), A_replica->view(), min, nv->min());
     */
    return false;
  }

  base = min + 1;

  Digest d;
  if (!vc->ckpt(min, d))
  {
    /*
     fprintf(
     stderr,
     "A_Replica %i, primary %i, view %qd, check_new_view(). !vc->chkpt(%qd, d)\n",
     A_replica->id(), A_replica->primary(), A_replica->view(), min);
     */
    return false;
  }

  int n_le = 1;
  int n_proofs = 1;

  // Search view-changes for proofs
  Digest dd;
  for (int i = 0; i < vcs.size(); i++)
  {
    if (i != cid && vcs[i].vc)
    {
      if (vcs[i].vc->ckpt(min, dd) && dd == d)
        n_proofs++;
      if (vcs[i].vc->last_stable() <= min)
        n_le++;
    }
  }

  if (n_proofs < A_node->f() + 1 || n_le < A_node->n_f())
  {
    /*
     fprintf(
     stderr,
     "A_Replica %i, primary %i, view %qd, check_new_view(). n_proofs = %i, f+1 = %i, n_le = %i, n_f = %i\n",
     A_replica->id(), A_replica->primary(), A_replica->view(), n_proofs, A_node->f()
     + 1, n_le, A_node->n_f());
     */
    return false;
  }

  chosen_ckpt = 0;

  // A_Checkpoint is correct. Check the value of nv->max(): this value
  // is correct if there exist 2f+1 view change messages that do not
  // propose any pre-prepared or prepared request with sequence number
  // greater than or equal to nv->max().
  int n_lt = 0;
  for (int i = 0; i < vcs.size(); i++)
  {
    A_View_change* vc = vcs[i].vc;
    if (vc == 0)
      continue;

    if (vc->max_seqno() < nv->max())
      n_lt++;
  }

  if (n_lt < A_node->n_f())
  {
    /*
     fprintf(
     stderr,
     "A_Replica %i, primary %i, view %qd, check_new_view(). n_lt = %i, n_f = %i\n",
     A_replica->id(), A_replica->primary(), A_replica->view(), n_lt, A_node->n_f());
     */

    return false;
  }

  // nv->max() is correct. Check requests.
  max = nv->max();

  if (base == nv->max())
  {
    is_complete = true;
    Seqno ks = known_stable();

    /*
     fprintf(
     stderr,
     "A_Replica %i, primary %i, view %qd. calling process_new_view() in make_new_view()\n",
     A_replica->id(), A_replica->primary(), A_replica->view());
     */
    A_replica->process_new_view(min, d, nv->max(), ks);

    return true;
  }

  reqs._enlarge_by(max - min - 1);
  comp_reqs._enlarge_by(max - min - 1);
  for (int i = 0; i < comp_reqs.size(); i++)
  {
    comp_reqs[i] = -1;
  }

  for (Seqno i = base; i < nv->max(); i++)
  {
    int vci = nv->which_picked(i);
    vc = vcs[vci].vc;

    if (i <= vc->last_stable())
    {
      /*
       fprintf(
       stderr,
       "A_Replica %i, primary %i, view %qd, check_new_view(). i = %qd, last_stable= %qd\n",
       A_replica->id(), A_replica->primary(), A_replica->view(), i,
       vc->last_stable());
       */
      return false;
    }

    reqs[i - base]._enlarge_by(1);
    Req_sum& cur = reqs[i - base].high();
    cur.v = vc->req(i, cur.d);
    cur.n_le = 0;
    cur.pi.zero();
    cur.id = vc->id();
    cur.n_proofs = 0;
    cur.n_pproofs = 0;
    cur.r_pproofs.clear();

    // Search view-changes for proofs
    for (int j = 0; j < vcs.size(); j++)
    {
      if (vcs[j].vc)
      {
        get_proofs(cur, vcs[j].vc, i);
        Digest dd;
        if (vcs[j].vc->req(i, dd) <= cur.v)
          cur.n_le++;
      }
    }

    if (cur.n_proofs < A_node->f() + 1 || cur.n_le < A_node->n_f())
    {
      /*
       fprintf(
       stderr,
       "A_Replica %i, primary %i, view %qd, check_new_view(). cur.n_proofs = %i, f + 1 = %i, cur.n_le = %i, n_f = %i\n",
       A_replica->id(), A_replica->primary(), A_replica->view(), cur.n_proofs,
       A_node->f() + 1, cur.n_le, A_node->n_f());
       */
      return false;
    }
    else
    {
      /*
       fprintf(
       stderr,
       "A_Replica %i, primary %i, view %qd, check_new_view(). Calling check_comp()\n",
       A_replica->id(), A_replica->primary(), A_replica->view());
       */
      check_comp(cur, i, 0);
    }
  }

  return true;
}

A_Pre_prepare* A_NV_info::fetch_request(Seqno n, Digest &d)
{
  th_assert(is_complete, "Invalid state");
  th_assert(n > nv->min() && n < nv->max(), "Invalid arguments");

  A_Pre_prepare* pp = 0;
  Digest null;
  View pv = vcs[nv->which_picked(n)].vc->req(n, d);

  if (pv >= 0 && d != null)
  {
    // Normal request
    pp = reqs[n - base][comp_reqs[n - base]].pi.pre_prepare();
    reqs[n - base][comp_reqs[n - base]].pi.zero();

    th_assert(pp != 0, "Invalid state");
  }
  else
  {
    // Null request
    A_Req_queue empty;
    pp = new A_Pre_prepare(v, n, empty);
    d = pp->digest();
  }

  if (A_node->primary(v) == A_node->id())
    pp->re_authenticate();

  return pp;
}

void A_NV_info::set_received_vcs(A_Status *m)
{
  if (A_node->primary(v) != A_node->id())
  {
    // Not primary.
    Digest d;
    for (int i = 0; i < A_node->n(); i++)
    {
      if (vcs[i].vc || !nv->view_change(i, d))
        m->mark_vcs(i);
    }
  }
  else
  {
    for (int i = 0; i < A_node->n(); i++)
    {
      if (vcs[i].vc && nv->view_change(i))
        m->mark_vcs(i);
    }
  }
}

void A_NV_info::set_missing_pps(A_Status *m)
{
  for (Seqno i = base; i < max; i++)
  {
    if (comp_reqs[i - base] >= 0)
      continue;

    Array<Req_sum>& reqsi = reqs[i - base];
    View vpp = v;
    bool need_proofs = false;
    BR_map mrmap = ~0LL;

    // For each sequence number, determine the minimum view vpp for which
    // there is a missing pre-prepare for a proven request.
    for (int j = 0; j < reqsi.size(); j++)
    {
      Req_sum& cur = reqsi[j];
      if (cur.v >= 0 && cur.v < vpp && cur.n_proofs >= A_node->f() + 1
          && cur.n_le >= A_node->n_f())
      {
        vpp = cur.v;

        if (cur.n_pproofs <= A_node->f())
          need_proofs = true;
        else if (cur.pi.pre_prepare())
          mrmap &= cur.pi.missing_reqs();
      }
    }

    // Ask for any pre-prepares for that sequence number with view
    // greater than or equal to vpp.
    if (vpp < v)
      m->append_pps(vpp, i, mrmap, need_proofs);
  }
}

void A_NV_info::add_missing(A_Pre_prepare* pp)
{
  Seqno ppn = pp->seqno();

  if (chosen_ckpt >= 0 && ppn > min && ppn < max && comp_reqs[ppn - base] < 0)
  {
    Array<Req_sum>& reqspp = reqs[ppn - base];

    for (int j = 0; j < reqspp.size(); j++)
    {
      Req_sum& cur = reqspp[j];

      if (cur.d == pp->digest())
      {
        if (cur.pi.pre_prepare() == 0 && pp->check_digest())
        {
          cur.pi.add(pp->clone(v));
          check_comp(cur, ppn, j);
        }
        break;
      }
    }
  }

  delete pp;
}

void A_NV_info::add_missing(Digest& rd, Seqno ppn, int i)
{
  if (chosen_ckpt >= 0 && ppn > min && ppn < max && comp_reqs[ppn - base] < 0)
  {
    Array<Req_sum>& reqspp = reqs[ppn - base];

    for (int j = 0; j < reqspp.size(); j++)
    {
      Req_sum& cur = reqspp[j];
      cur.pi.add(rd, i);
      check_comp(cur, ppn, j);
      if (complete())
        break;
    }
  }
}

void A_NV_info::add_missing(A_Prepare* p)
{
  Seqno pn = p->seqno();

  if (chosen_ckpt >= 0 && pn > min && pn < max && comp_reqs[pn - base] < 0)
  {
    Array<Req_sum>& reqsp = reqs[pn - base];

    for (int j = 0; j < reqsp.size(); j++)
    {
      Req_sum& cur = reqsp[j];

      if (cur.d == p->digest())
      {
        if (cur.n_pproofs <= A_node->f() && !cur.r_pproofs.test(p->id()))
        {
          cur.n_pproofs++;
          cur.r_pproofs.set(p->id());
          check_comp(cur, pn, j);
        }
        break;
      }
    }
  }

  delete p;
}

void A_NV_info::mark_stable(Seqno ls)
{
  if (v > 0 && !is_complete && chosen_ckpt >= 0 && ls >= max
      && A_node->primary(v) != A_node->id())
  {
    // If I am not the primary, I can use the fact that ls is stable
    // to trim the number of pre-prepares I need proofs for.
    is_complete = true;

    /*
     fprintf(
     stderr,
     "A_Replica %i, primary %i, view %qd, mark_stable(). Calling process_new_view()\n",
     A_replica->id(), A_replica->primary(), A_replica->view());
     */
    A_replica->process_new_view(ls, Digest(), ls, ls);
  }
}
