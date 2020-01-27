#ifndef _C_Abort_certificate_h
#define _C_Abort_certificate_h 1

#include "Array.h"
#include "types.h"
#include "C_Abort.h"
#include "Bitmap.h"

class Ac_entry
{
public:
   inline Ac_entry()
   {
   }

   inline Ac_entry(C_Abort *a)
   {
      abort = a;
   }

   inline ~Ac_entry()
   {
      delete abort;
   }

   C_Abort *abort;
};

class C_Abort_certificate
{

public:

   C_Abort_certificate(int comp, int num_rep);

   ~C_Abort_certificate();

   bool add(C_Abort *abort);

   void clear();

   int size() const;

   bool is_complete() const;

   Ac_entry* operator[](int index) const;

private:
   Array<Ac_entry*> ac;
   int count;
   int complete;
};

inline int C_Abort_certificate::size() const
{
   return ac.size();
}

inline bool C_Abort_certificate::is_complete() const
{
   return count >= complete;
}

inline Ac_entry* C_Abort_certificate::operator[](int index) const
{
   th_assert((index >= 0) && (index < ac.size()), "array index out of bounds");
   return ac[index];
}

#endif
