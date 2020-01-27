#ifndef _C_Principal_h
#define _C_Principal_h 1

#include <string.h>
#include <sys/time.h>
#include "types.h"
#include "C_Cryptography.h"

#include "Traces.h"

extern "C" {
#include "umac.h"
}

class rabin_pub;

class C_Principal {
public:
	C_Principal(int i, int num_principals, Addr TCP_a, Addr TCP_a_for_clients, char *p=NULL);
	// Effects: Creates a new C_Principal object.

	virtual ~C_Principal();
	// Effects: Deallocates all the storage associated with principal.

	int pid() const;
	// Effects: Returns the principal identifier.

	//
	// Cryptography:
	//
	bool verify_mac(const char *src, unsigned src_len, const char *mac,
			const char *unonce);
	// Effects: Returns true iff "mac" is a valid MAC generated for
	// "src_len" bytes starting at "src".

	void gen_mac(const char *src, unsigned src_len, char *dst, int dest_pid,
			const char *unonce);
	// Requires: "dst" can hold at least "MAC_size" bytes.
	// Effects: Generates a MAC (with MAC_size bytes) using the key for dest_pid and
	// places it in "dst".  The MAC authenticates "src_len" bytes
	// starting at "src".

	inline static long long new_umac_nonce() {
		return ++umac_nonce;
	}

	Addr TCP_addr;
	Addr TCP_addr_for_clients;

	int sig_size() const;
	// Effects: Returns the size of signatures generated by this principal.

	bool verify_signature(const char *src, unsigned src_len, const char *sig, 
			bool allow_self=false);
	// Requires: "sig" is at least sig_size() bytes.
	// Effects: Checks a signature "sig" (from this principal) for
	// "src_len" bytes starting at "src". If "allow_self" is false, it
	// always returns false if "this->id == PBFT_R_node->id()"; otherwise,
	// returns true if signature is valid.

	unsigned encrypt(const char *src, unsigned src_len, char *dst, unsigned dst_len);
	// Effects: Encrypts "src_len" bytes starting at "src" using this
	// principal's public-key and places up to "dst_len" of the result in "dst".
	// Returns the number of bytes placed in "dst".


private:
	int id;
	rabin_pub *pkey;
	int ssize;                // signature size

	Request_id last_fetch; // Last request_id in a fetch message from this principal

	// UMAC contexts used to generate MACs for incoming and outgoing messages
	umac_ctx_t* ctxs;

	static long long umac_nonce;
	int num_principals;
};

inline int C_Principal::pid() const {
	return id;
}

inline int C_Principal::sig_size() const { return ssize; }

#endif // _C_Principal_h