/*
 * mempool.c --
 *
 * $Id$
 */

#ifndef lint
static const char rcsid[] = "$Id$";
#endif

#include "common.h"
#include "mempool.h"

mempool_t *mempool_create(mempool_t *pool, int nchunks, size_t chunk_size)
{
	mempool_t *mypool;

	if (pool) mypool = pool;
	else mypool = (mempool_t *) my_calloc(1, sizeof(*pool));

	if (!mypool) return((mempool_t *)0);

	/* We need at least sizeof(char *) to keep track of free chunks. */
	if (chunk_size < sizeof(char *)) chunk_size = sizeof(char *);

	mypool->chunk_size = chunk_size;
	mypool->nchunks = nchunks;
	mypool->free_chunk_ptr = (char *)0;
	mypool->pools = (char *)0;

	mempool_grow(mypool, nchunks);

	return(mypool);
}

int mempool_destroy(mempool_t *pool)
{
	char *ptr, *next;

	if (!pool) return(0);
	for (ptr = pool->pools; ptr; ptr = next) {
		next = *(char **)ptr;
		free(ptr);
	}
	free(pool);
	return(0);
}

int mempool_grow(mempool_t *pool, int nchunks)
{
	char *ptr, *ptr_start;

	ptr = (char *) my_calloc(1, sizeof(char *) + pool->chunk_size * nchunks);
	if (!ptr) return(1);

	/* Add this pool to the list of pools. */
	if (pool->pools) *(char **)ptr = pool->pools;
	pool->pools = ptr;

	/* Now initialize all the chunks in this new pool. */
	ptr += sizeof(char *);
	ptr_start = ptr;
	while (nchunks--) {
		*(char **)ptr = ptr + pool->chunk_size;
		ptr += pool->chunk_size;
	}

	/* Point to last valid block. */
	ptr -= pool->chunk_size;

	/* Add a link to the previous first free chunk. */
	*(char **)ptr = pool->free_chunk_ptr;

	/* Set our first chunk as the first free chunk. */
	pool->free_chunk_ptr = ptr_start;

	return(0);
}

void *mempool_get_chunk(mempool_t *pool)
{
	register char *ptr;

	/* See if we need more memory. */
	if (!pool->free_chunk_ptr && mempool_grow(pool, pool->nchunks)) return((void *)0);
	ptr = pool->free_chunk_ptr;

	/* Update free_chunk_ptr. */
	pool->free_chunk_ptr = *(char **)ptr;
	return((void *)ptr);
}

int mempool_free_chunk(mempool_t *pool, void *chunk)
{
	*(char **)chunk = pool->free_chunk_ptr;
	pool->free_chunk_ptr = (char *)chunk;
	return(0);
}
