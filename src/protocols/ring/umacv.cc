#include "umac.h"
#include "umacv.h"

int
umacv(umac_ctx_t ctx, const struct umacvdata* udv, int udvcount,
		char tag[], char nonce[8])
{
	umac_reset(ctx);
	for (int i=0; i<udvcount; i++) {
		if (udv[i].buffer == NULL)
			break;
		
		// apply transformers
		if (udv[i].transformers != 0) {
			struct data_transformer* dta = udv[i].transformers;
			for (int j=0; j<udv[i].num_transformers; j++) {
				if (dta[j].tf == NULL)
					break;
				dta[j].tf(udv[i].buffer, APPLY, dta[j].data);
			}
		}

		// do umac
		umac_update(ctx, udv[i].buffer, udv[i].size);

		// restore.
		// needs to go in the oposite direction
		if (udv[i].transformers != 0) {
			struct data_transformer* dta = udv[i].transformers;
			for (int j=udv[i].num_transformers-1; j>=0; j++) {
				if (dta[j].tf == NULL)
					break;
				dta[j].tf(udv[i].buffer, RESTORE, dta[j].data);
			}
		}
	}
	return umac_final(ctx, tag, nonce);
}

