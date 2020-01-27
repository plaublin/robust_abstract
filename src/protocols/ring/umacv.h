#ifndef UMACV_H
#define UMACV_H

#include <types.h>
#include "umac.h"

// defines the direction
// each transformer function can either apply itself
// or restore to the previous state
// example: removing seqno
//			apply will zero it, and store old value
//			restore will return the old value
typedef enum {
    APPLY = 0,
    RESTORE = 1
} transform_direction;

// takes a pointer to the data to transform, and data to embed
typedef void(*dt_func_ptr)(char*,transform_direction,void*);

// contains:
// the pointer to the transformer function
// the pointer to the data to embed (the second parameter);
struct data_transformer {
    dt_func_ptr tf;
    void		*data;
};

struct umacvdata {
    char* buffer;
    long  size;

    // uses the same trick as umacvdata:
    // pointer to an array
    // and the number of elements
    struct data_transformer* transformers;
    int   num_transformers;
};

int umacv(umac_ctx_t ctx, const struct umacvdata* udv, int udvcount, 
		char tag[], char nonce[8]);
#endif
