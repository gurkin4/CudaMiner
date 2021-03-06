#include "cpuminer-config.h"
#include "miner.h"
#include "salsa_kernel.h"

#include <string.h>
#include <stdint.h>

// an alternative SHA-3 implementation, easier portable to CUDA IMHO
#include "sha3.h"
#include "keccak.h"

int scanhash_keccak(int thr_id, uint32_t *pdata, const uint32_t *ptarget,
	uint32_t max_nonce, struct timeval *tv_start, struct timeval *tv_end, unsigned long *hashes_done)
{
	int throughput = cuda_throughput(thr_id);

	gettimeofday(tv_start, NULL);

	uint32_t n = pdata[19] - 1;
	const uint32_t first_nonce = pdata[19];
	
	// TESTING ONLY
//	((uint32_t*)ptarget)[7] = 0x0000000f;
	
	const uint32_t Htarg = ptarget[7];

	uint32_t endiandata[20];
	for (int kk=0; kk < 20; kk++)
		be32enc(&endiandata[kk], ((uint32_t*)pdata)[kk]);

	cuda_prepare_keccak256(thr_id, endiandata, ptarget);

	uint32_t *cuda_hash64 = (uint32_t *)cuda_hashbuffer(thr_id, 0);
	memset(cuda_hash64, 0xff, throughput * 8 * sizeof(uint32_t));
	bool validate = false;
	do {
		int nonce = n+1;

		uint32_t result = cuda_do_keccak256(thr_id, 0, cuda_hash64, nonce, throughput, validate);
		n += throughput;

//		cuda_scrypt_sync(thr_id, 0);

		// optional full CPU based validation
		if (validate)
		{
			for (int i=0; i < throughput; ++i)
			{
				uint32_t hash64[8];
				be32enc(&endiandata[19], nonce+i); 
				crypto_hash( (unsigned char*)hash64, (unsigned char*)&endiandata[0], 80 );
	
				if (memcmp(hash64, &cuda_hash64[8*i], 32))
					fprintf(stderr, "CPU and CUDA hashes (i=%d) differ!\n", i);
			}
		}

#if 0
		// copying all hashes from device to host is too slow, hence the following
		// CPU based hash verification loop is now commented out
		for (int i=0; i < throughput; ++i)
		{
			uint32_t *hash64 = &cuda_hash64[8*i];
			if (hash64[7] <= Htarg &&
					fulltest(hash64, ptarget)) {
				pdata[19] = nonce+i;
				*hashes_done = n - first_nonce + 1;
				gettimeofday(tv_end, NULL);
				return true;
			}
		}
#endif
		if (result != 0xffffffff)
		{
			uint32_t hash64[8];
			be32enc(&endiandata[19], result);
			crypto_hash( (unsigned char*)hash64, (unsigned char*)&endiandata[0], 80 );
			if (result >= nonce && result < nonce+throughput && hash64[7] <= Htarg && fulltest(hash64, ptarget)) {
				pdata[19] = result;
				*hashes_done = n - first_nonce + 1;
				gettimeofday(tv_end, NULL);
				return true;
			} else {
				applog(LOG_INFO, "GPU #%d: %s result for nonce $%08x does not validate on CPU!", device_map[thr_id], device_name[thr_id], result);
			}
		}
	} while (n < max_nonce && !work_restart[thr_id].restart);
	
	*hashes_done = n - first_nonce + 1;
	pdata[19] = n;
	gettimeofday(tv_end, NULL);
	return 0;
}
