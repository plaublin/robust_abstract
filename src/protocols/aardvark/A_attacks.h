#ifndef __A_ATTACKS_H_
#define __A_ATTACKS_H_

#include "A_Node.h"
#include "A_Request.h"

extern A_Node* A_node;

// [attacks] API.
#define A_NONE 0
#define A_FLOOD_PROT   1
#define A_FLOOD_MAX    2
#define A_CLIENT_BIAS  4
bool A_clientBias();
bool A_floodMax();
bool A_floodProtocol();


// [attack] Attacks.
// We will fix the attacks to specific replicas.
// Only the first primary biases on the first client,
// Only the last primary floods (either max or protocol).
extern int A_attack_mode;

inline void A_setAttack(int mode)
{
  if (mode == A_NONE) return;
  switch (mode) {
  case A_FLOOD_MAX:
    A_attack_mode |= mode;
    fprintf(stderr, "Attacking with flood max.\n");
    break;
  case A_FLOOD_PROT:
    A_attack_mode |= mode;
    fprintf(stderr, "Attacking with flood protocol.\n");
    break;
  case A_CLIENT_BIAS:
    A_attack_mode |= mode;
    fprintf(stderr, "Attacking with client bias.\n");
    break;
  default:
    break;
  }
}

inline bool A_clientBias(A_Request *m)
{
  return (A_attack_mode & A_CLIENT_BIAS) && m->client_id() == A_node->n() && A_node->id() == 0;
}

inline bool A_floodMax() 
{
  return (A_attack_mode & A_FLOOD_MAX) && A_node->id() == 3;
}

inline bool A_floodProtocol()
{
  return (A_attack_mode & A_FLOOD_PROT) && A_node->id() == 3;
}

#endif
