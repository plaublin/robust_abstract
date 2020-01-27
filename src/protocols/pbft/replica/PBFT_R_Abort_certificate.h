#ifndef _PBFT_R_Abort_certificate_h
#define _PBFT_R_Abort_certificate_h 1

#include "Array.h"
#include "types.h"
#include "PBFT_R_Abort.h"
#include "PBFT_R_Bitmap.h"

class Ac_entry
{
public:
   inline Ac_entry()
   {
   }

   inline Ac_entry(PBFT_R_Abort *a)
   {
      abort = a;
   }

   inline ~Ac_entry()
   {
      delete abort;
   }

   PBFT_R_Abort *abort;
};

class PBFT_R_Abort_certificate
{

public:

   PBFT_R_Abort_certificate(int comp, int num_rep);

   ~PBFT_R_Abort_certificate();

   bool add(PBFT_R_Abort *abort);

   void clear();

   int size() const;

   bool is_complete() const;

   Ac_entry* operator[](int index) const;

private:
   Array<Ac_entry*> ac;
   int count;
   int complete;
};

inline int PBFT_R_Abort_certificate::size() const
{
   return ac.size();
}

inline bool PBFT_R_Abort_certificate::is_complete() const
{
   return count >= complete;
}

inline Ac_entry* PBFT_R_Abort_certificate::operator[](int index) const
{
   th_assert((index >= 0) && (index < ac.size()), "array index out of bounds");
   return ac[index];
}

#endif
