/*
 * mempool.h --
 *
 * $Id$
 */

#ifndef _MEMPOOL_H
#define _MEMPOOL_H

#include <sys/types.h>

typedef struct mempool_b {
	size_t chunk_size;
	int nchunks;
	char *free_chunk_ptr;
	char *pools;
} mempool_t;

mempool_t *mempool_create(mempool_t *pool, int nchunks, size_t chunk_size);
int mempool_destroy(mempool_t *pool);
int mempool_grow(mempool_t *pool, int nchunks);
void *mempool_get_chunk(mempool_t *pool);
int mempool_free_chunk(mempool_t *pool, void *chunk);

#endif				/* _MEMPOOL_H */
