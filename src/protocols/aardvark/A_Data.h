#ifndef _A_Data_h
#define _A_Data_h 1

#include "types.h"
#include "A_Message.h"
#include "Partition.h"
#include "A_State_defs.h"


// 
// A_Data messages have the following format:
//
struct A_Data_rep : public A_Message_rep {
  int index;        // index of this page within level
  int padding;
  Seqno lm;         // Seqno of last checkpoint in which data was modified

  char data[Block_size];

};

class A_Data : public A_Message {
  // 
  // A_Data messages
  //
public:
  A_Data(int i, Seqno lm, char *data);
  // Effects: Creates a new A_Data message. 
  //          (if we are using BASE) "totalsz" is the size of the object,
  //          chunkn is the number of the fragment that we are sending
  //          and "data" points to the beginning of the fragment (not to
  //          the beginning of the object - XXX change this?)

  int index() const;
  // Effects: Returns index of data page

  Seqno last_mod() const;
  // Effects: Returns the seqno of last checkpoint in which data was
  // modified


  char *data() const;
  // Effects: Returns a pointer to the data page.

  static bool convert(A_Message *m1, A_Data *&m2);
  // Effects: If "m1" has the right size and tag, casts "m1" to a
  // "A_Data" pointer, returns the pointer in "m2" and returns
  // true. Otherwise, it returns false. 

private:
  A_Data_rep &rep() const;
  // Effects: Casts contents to a A_Data_rep&
};

inline A_Data_rep &A_Data::rep() const { 
  return *((A_Data_rep*)msg); 
}

inline int A_Data::index() const { return rep().index; }

inline Seqno A_Data::last_mod() const { return rep().lm; }


inline char *A_Data::data() const { return rep().data; }


#endif // _A_Data_h
