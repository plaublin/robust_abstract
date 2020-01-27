#ifndef _PBFT_R_Smasher_h
#define _PBFT_R_Smasher_h

#include "types.h"
#include "Digest.h"
#include "Array.h"
#include "SCSet.h"
#include "PBFT_R_Abort.h"
#include "PBFT_R_Abort_certificate.h"
#include "AbortHistoryElement.h"

class PBFT_R_Smasher {
    public:
    PBFT_R_Smasher(unsigned int s, int faults, PBFT_R_Abort_certificate& a):
	size(s), t(faults), aborts(a), seeder(a.size()), ah(NULL), missing(NULL) {};
    ~PBFT_R_Smasher() {};

    void process(int id);
    // Effects: processes abort histories to extract relevant data
    // id is the id of the replica on which computation happens

    AbortHistory *get_ah() { return ah; }
    // Effects: gets the extracted history in one ordered array

    AbortHistory *get_missing() { return missing; }
    // Effects: gets the missing elementsin one ordered array


    private:
	unsigned int size;
	unsigned int t;
	PBFT_R_Abort_certificate& aborts;
	SCSet<AbortHistoryElement> seeder;
	AbortHistory *ah;
	AbortHistory *missing;
};


#endif // _PBFT_R_Smasher_h
