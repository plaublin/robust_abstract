#include "A_Node.h"
#include "A_Prepared_cert.h"

#include "A_Certificate.t"
template class A_Certificate<A_Prepare> ;

A_Prepared_cert::A_Prepared_cert() :
  pc(A_node->f() * 2), primary(false)
{
}

A_Prepared_cert::~A_Prepared_cert()
{
  pi.clear();
}

bool A_Prepared_cert::is_pp_correct()
{
  if (pi.pre_prepare())
  {
    A_Certificate<A_Prepare>::Val_iter viter(&pc);
    int vc;
    A_Prepare* val;
    while (viter.get(val, vc))
    {
      if (vc >= A_node->f() && pi.pre_prepare()->match(val))
      {
        return true;
      }
    }
  }
  return false;
}

bool A_Prepared_cert::add(A_Pre_prepare *m)
{
  if (pi.pre_prepare() == 0)
  {
    A_Prepare* p = pc.mine();

    if (p == 0)
    {
      if (m->verify())
      {
        pi.add(m);
        return true;
      }
      else
      {
        // PL: this message is displayed with the new mac attack
        //fprintf(stderr, "View %qd. PP verify is false\n", A_node->view());
      }

      if (m->verify(A_Pre_prepare::NRC))
      {
        // Check if there is some value that matches pp and has f
        // senders.
        A_Certificate<A_Prepare>::Val_iter viter(&pc);
        int vc;
        A_Prepare* val;
        while (viter.get(val, vc))
        {
          if (vc >= A_node->f() && m->match(val))
          {
            pi.add(m);
            return true;
          }
        }
      }
    }
    else
    {
      // If we sent a prepare, we only accept a matching pre-prepare.
      if (m->match(p) && m->verify(A_Pre_prepare::NRC))
      {
        pi.add(m);
        return true;
      }
    }
  }
  delete m;
  return false;
}

bool A_Prepared_cert::encode(FILE* o)
{
  bool ret = pc.encode(o);
  ret &= pi.encode(o);
  int sz = fwrite(&primary, sizeof(bool), 1, o);
  return ret & (sz == 1);
}

bool A_Prepared_cert::decode(FILE* i)
{
  th_assert(pi.pre_prepare() == 0, "Invalid state");

  bool ret = pc.decode(i);
  ret &= pi.decode(i);
  int sz = fread(&primary, sizeof(bool), 1, i);
  t_sent = A_zeroTime();

  return ret & (sz == 1);
}

bool A_Prepared_cert::is_empty() const
{
  return pi.pre_prepare() == 0 && pc.is_empty();
}
