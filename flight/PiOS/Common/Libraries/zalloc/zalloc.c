/**
 * @file	zalloc.c
 * @brief	self contained low-overhead memory pool/allocation subsystem
 *
 * This is a lightly modified version of the Dillon zalloc as documented below.
 *
 * Modifications (c) 2011 Michael Smith.
 *
 * Note that the original was distributed without explicit copyright or
 * license information associated, but it is believed to be (c) Matt Dillon
 * and to be generally available for use without restrictions.
 */
/*
 * LIB/MEMORY/ZALLOC.C	- self contained low-overhead memory pool/allocation 
 *			  subsystem
 *
 *	This subsystem implements memory pools and memory allocation 
 *	routines.
 *
 *	Pools are managed via a linked list of 'free' areas.  Allocating
 *	memory creates holes in the freelist, freeing memory fills them.
 *	Since the freelist consists only of free memory areas, it is possible
 *	to allocate the entire pool without incuring any structural overhead.
 *
 *	The system works best when allocating similarly-sized chunks of
 *	memory.  Care must be taken to avoid fragmentation when 
 *	allocating/deallocating dissimilar chunks.
 *
 *	When a memory pool is first allocated, the entire pool is marked as
 *	allocated.  This is done mainly because we do not want to modify any
 *	portion of a pool's data area until we are given permission.  The
 *	caller must explicitly deallocate portions of the pool to make them
 *	available.
 *
 *	z[n]xalloc() works like z[n]alloc() but the allocation is made from
 *	within the specified address range.  If the segment could not be 
 *	allocated, NULL is returned.  WARNING!  The address range will be
 *	aligned to an 8 or 16 byte boundry depending on the cpu so if you
 *	give an unaligned address range, unexpected results may occur.
 *
 *	If a standard allocation fails, the reclaim function will be called
 *	to recover some space.  This usually causes other portions of the
 *	same pool to be released.  Memory allocations at this low level
 *	should not block but you can do that too in your reclaim function
 *	if you want.  Reclaim does not function when z[n]xalloc() is used,
 *	only for z[n]alloc().
 *
 *	Allocation and frees of 0 bytes are valid operations.
 */

#include "zalloc_private.h"
#include <string.h>

/*
 * _zlock() - Lock the allocator
 */
static void
_zlock(void)
{
	if (&zlock)
		zlock();
}

/*
 * _zunlock() - Unlock the allocator
 */
static void
_zunlock(void)
{
	if (&zunlock)
		zunlock();
}

/*
 * znop() - panic function if none supplied.
 */

void
znop(const char *ctl, ...)
{
	for (;;)
		;
}

/*
 * znot() - reclaim function if none supplied
 */

int
znot(struct MemPool *memPool, iaddr_t bytes)
{
	return(-1);
}

/*
 * zalloc() -	allocate and zero memory from pool.  Call reclaim
 *		and retry if appropriate, return NULL if unable to allocate
 *		memory.
 */

void *
zalloc(MemPool *mp, iaddr_t bytes)
{
	void *ptr;

	if ((ptr = znalloc(mp, bytes)) != NULL)
		memset(ptr, 0, bytes);
	return(ptr);
}

/*
 * zallocAlign() - allocate and zero memory from pool, enforce specified
 *		   alignment (must be power of 2) on allocated memory.
 */

void *
zallocAlign(struct MemPool *mp, iaddr_t bytes, iaddr_t align)
{
	void *ptr;

	--align;
	bytes = (bytes + align) & ~align;

	if ((ptr = znalloc(mp, bytes)) != NULL) {
		memset(ptr, 0, bytes);
	}
	return(ptr);
}

/*
 * znalloc() -	allocate memory (without zeroing) from pool.  Call reclaim
 *		and retry if appropriate, return NULL if unable to allocate
 *		memory.
 */
/* @todo If we don't want the reclaim behaviour this can be made considerably simpler */
void *
znalloc(MemPool *mp, iaddr_t bytes)
{
	void	*result = NULL;

	_zlock();

	/*
	 * align according to pool object size (can be 0).  This is
	 * inclusive of the MEMNODE_SIZE_MASK minimum alignment.
	 *
	 */
	bytes = (bytes + MEMNODE_SIZE_MASK) & ~MEMNODE_SIZE_MASK;

	if (bytes > 0) {

		do {
			/*
			 * locate freelist entry big enough to hold the object.  If all objects
			 * are the same size, this is a constant-time function.
			 */

			if (bytes <= mp->mp_Size - mp->mp_Used) {
				MemNode **pmn;
				MemNode *mn;

				for (pmn = &mp->mp_First; (mn=*pmn) != NULL; pmn = &mn->mr_Next) {
					if (bytes > mn->mr_Bytes)
						continue;

					/*
					 *  Cut a chunk of memory out of the beginning of this
					 *  block and fixup the link appropriately.
					 */

					{
						char *ptr = (char *)mn;

						if (mn->mr_Bytes == bytes) {
							*pmn = mn->mr_Next;
						} else {
							mn = (MemNode *)((char *)mn + bytes);
							mn->mr_Next  = ((MemNode *)ptr)->mr_Next;
							mn->mr_Bytes = ((MemNode *)ptr)->mr_Bytes - bytes;
							*pmn = mn;
						}
						mp->mp_Used += bytes;

						result = ptr;
						goto done;
					}
				}
			}
		} while (mp->mp_Reclaim(mp, bytes) == 0);
	}

done:
	_zunlock();
	return(result);
}

/*
 * z[n]xalloc() -  allocate memory from within a specific address region.
 *		   If allocating AT a specific address, then addr2 must be
 *		   set to addr1 + bytes (and this only works if addr1 is
 *		   already aligned).  addr1 and addr2 are aligned by
 *		   MEMNODE_SIZE_MASK + 1 (i.e. they wlill be 8 or 16 byte 
 *		   aligned depending on the machine core).
 */

void *
zxalloc(MemPool *mp, void *addr1, void *addr2, iaddr_t bytes)
{
	void *ptr;

	if ((ptr = znxalloc(mp, addr1, addr2, bytes)) != NULL)
		memset(ptr, 0, bytes);
	return(ptr);
}

void *
znxalloc(MemPool *mp, void *addr1, void *addr2, iaddr_t bytes)
{
	void	*result = NULL;

	/*
	 * align according to pool object size (can be 0).  This is
	 * inclusive of the MEMNODE_SIZE_MASK minimum alignment.
	 */
	bytes = (bytes + MEMNODE_SIZE_MASK) & ~MEMNODE_SIZE_MASK;
	addr1= (void *)(((iaddr_t)addr1 + MEMNODE_SIZE_MASK) & ~MEMNODE_SIZE_MASK);
	addr2= (void *)(((iaddr_t)addr2 + MEMNODE_SIZE_MASK) & ~MEMNODE_SIZE_MASK);

	if (bytes == 0)
		return((void *)addr1);

	/*
	 * Locate freelist entry big enough to hold the object that is within
	 * the allowed address range.
	 */
	_zlock();
	if (bytes <= mp->mp_Size - mp->mp_Used) {
		MemNode **pmn;
		MemNode *mn;

		for (pmn = &mp->mp_First; (mn = *pmn) != NULL; pmn = &mn->mr_Next) {
			int mrbytes = mn->mr_Bytes;
			int offset = 0;

			/*
			 * offset from base of mn to satisfy addr1.  0 or positive
			 */

			if ((char *)mn < (char *)addr1)
				offset = (char *)addr1 - (char *)mn;

			/*
			 * truncate mrbytes to satisfy addr2.  mrbytes may go negative
			 * if the mn is beyond the last acceptable address.
			 */

			if ((char *)mn + mrbytes > (char *)addr2)
				mrbytes = (saddr_t)((iaddr_t)addr2 - (iaddr_t)mn); /* signed */

			/*
			 * beyond last acceptable address.
			 *
			 * before first acceptable address (if offset > mrbytes, the
			 * second conditional will always succeed).
			 *
			 * area overlapping acceptable address range is not big enough.
			 */

			if (mrbytes < 0)
				break;

			if (mrbytes - offset < bytes)
				continue;

			/*
			 *  Cut a chunk of memory out of the block and fixup the link
			 *  appropriately.
			 *
			 *  If offset != 0, we have to cut a chunk out from the middle of
			 *  the block.
			 */

			if (offset) {
				MemNode *mnew = (MemNode *)((char *)mn + offset);

				mnew->mr_Bytes = mn->mr_Bytes - offset;
				mnew->mr_Next = mn->mr_Next;
				mn->mr_Bytes = offset;
				mn->mr_Next = mnew;
				pmn = &mn->mr_Next;
				mn = mnew;
			}
			{
				char *ptr = (char *)mn;
				if (mn->mr_Bytes == bytes) {
					*pmn = mn->mr_Next;
				} else {
					mn = (MemNode *)((char *)mn + bytes);
					mn->mr_Next  = ((MemNode *)ptr)->mr_Next;
					mn->mr_Bytes = ((MemNode *)ptr)->mr_Bytes - bytes;
					*pmn = mn;
				}
				mp->mp_Used += bytes;
				result = ptr;
				goto done;
			}
		}
	}
done:
	_zunlock();
	return(result);
}

/*
 * zfree() - free previously allocated memory
 */

void
zfree(MemPool *mp, void *ptr, iaddr_t bytes)
{
	/*
	 * align according to pool object size (can be 0).  This is
	 * inclusive of the MEMNODE_SIZE_MASK minimum alignment.
	 */
	bytes = (bytes + MEMNODE_SIZE_MASK) & ~MEMNODE_SIZE_MASK;

	if (bytes == 0)
		return;

	/*
	 * panic if illegal pointer
	 */

	if ((char *)ptr < (char *)mp->mp_Base ||
			(char *)ptr + bytes > (char *)mp->mp_End ||
			((iaddr_t)ptr & MEMNODE_SIZE_MASK) != 0
	) {
		mp->mp_Panic(
				"zfree(%s,0x%08lx,%d): wild pointer",
				mp->mp_Ident,
				(long)ptr,
				bytes
		);
	}

	/*
	 * free the segment
	 */
	_zlock();
	{
		MemNode **pmn;
		MemNode *mn;

		mp->mp_Used -= bytes;

		for (pmn = &mp->mp_First; (mn = *pmn) != NULL; pmn = &mn->mr_Next) {
			/*
			 * If area between last node and current node
			 *  - check range
			 *  - check merge with next area
			 *  - check merge with previous area
			 */
			if ((char *)ptr <= (char *)mn) {
				/*
				 * range check
				 */
				if ((char *)ptr + bytes > (char *)mn) {
					mp->mp_Panic("zfree(%s,0x%08lx,%d): corrupt memlist1",
							mp->mp_Ident,
							(long)ptr,
							bytes
					);
				}

				/*
				 * merge against next area or create independant area
				 */

				if ((char *)ptr + bytes == (char *)mn) {
					((MemNode *)ptr)->mr_Next = mn->mr_Next;
					((MemNode *)ptr)->mr_Bytes= bytes + mn->mr_Bytes;
				} else {
					((MemNode *)ptr)->mr_Next = mn;
					((MemNode *)ptr)->mr_Bytes= bytes;
				}
				*pmn = mn = (MemNode *)ptr;

				/*
				 * merge against previous area (if there is a previous
				 * area).
				 */

				if (pmn != &mp->mp_First) {
					if ((char*)pmn + ((MemNode*)pmn)->mr_Bytes == (char*)ptr) {
						((MemNode *)pmn)->mr_Next = mn->mr_Next;
						((MemNode *)pmn)->mr_Bytes += mn->mr_Bytes;
						mn = (MemNode *)pmn;
					}
				}
				goto done;
				/* NOT REACHED */
			}
			if ((char *)ptr < (char *)mn + mn->mr_Bytes) {
				mp->mp_Panic("zfree(%s,0x%08lx,%d): corrupt memlist2",
						mp->mp_Ident,
						(long)ptr,
						bytes
				);
			}
		}
		/*
		 * We are beyond the last MemNode, append new MemNode.  Merge against
		 * previous area if possible.
		 */
		if (pmn == &mp->mp_First ||
				(char *)pmn + ((MemNode *)pmn)->mr_Bytes != (char *)ptr
		) {
			((MemNode *)ptr)->mr_Next = NULL;
			((MemNode *)ptr)->mr_Bytes = bytes;
			*pmn = (MemNode *)ptr;
			mn = (MemNode *)ptr;
		} else {
			((MemNode *)pmn)->mr_Bytes += bytes;
			mn = (MemNode *)pmn;
		}
	}
done:
	_zunlock();
}

/*
 * zallocStr() - allocate memory and copy string.
 */

char *
zallocStr(MemPool *mp, const char *s, int slen)
{
	char *ptr;

	if (slen < 0)
		slen = strlen(s);
	if ((ptr = znalloc(mp, slen + 1)) != NULL) {
		memcpy(ptr, s, slen);
		ptr[slen] = 0;
	}
	return(ptr);
}

/*
 * zfreeStr() - free memory associated with an allocated string.
 */

void
zfreeStr(MemPool *mp, char *s)
{
	zfree(mp, s, strlen(s) + 1);
}

/*
 * zinitpool() - initialize a memory pool
 */

void 
zinitPool(
		MemPool *mp,
		const char *id,
		void (*fpanic)(const char *ctl, ...),
		int (*freclaim)(MemPool *memPool, iaddr_t bytes),
		void *pBase,
		iaddr_t pSize
) {
	iaddr_t	adjust;

	if (fpanic == NULL)
		fpanic = znop;
	if (freclaim == NULL)
		freclaim = znot;

	if (id != (const char *)-1)
		mp->mp_Ident = id;
	adjust = (iaddr_t)pBase;
	mp->mp_Base = (void *)((adjust + MEMNODE_SIZE_MASK) & ~MEMNODE_SIZE_MASK);
	adjust = (iaddr_t)pBase + pSize;
	mp->mp_End = (void *)(adjust & ~MEMNODE_SIZE_MASK);
	mp->mp_First = NULL;
	mp->mp_Size = (iaddr_t)mp->mp_End - (iaddr_t)mp->mp_Base;
	mp->mp_Used = pSize;
	mp->mp_Panic = fpanic;
	mp->mp_Reclaim = freclaim;
}

/*
 * zextendPool() - extend memory pool to cover additional space.
 *
 *		   Note: the added memory starts out as allocated, you
 *		   must free it to make it available to the memory subsystem.
 *
 *		   Note: mp_Size may not reflect (mp_End - mp_Base) range
 *		   due to other parts of the system doing their own sbrk()
 *		   calls.
 */

void
zextendPool(MemPool *mp, void *base, iaddr_t bytes)
{
	_zlock();
	if (mp->mp_Size == 0) {
		mp->mp_Base = base;
		mp->mp_Used = bytes;
	} else {
		void *pend = (char *)mp->mp_Base + mp->mp_Size;

		if (base < mp->mp_Base) {
			/* mp->mp_Size += (char *)mp->mp_Base - (char *)base; */
			mp->mp_Used += (char *)mp->mp_Base - (char *)base;
			mp->mp_Base = base;
		}
		base = (char *)base + bytes;
		if (base > pend) {
			/* mp->mp_Size += (char *)base - (char *)pend; */
			mp->mp_Used += (char *)base - (char *)pend;
		}
		mp->mp_End = (char *)mp->mp_Base + mp->mp_Size;
	}
	mp->mp_Size += bytes;
	_zunlock();
}

/*
 * zclearpool() - Free all memory associated with a memory pool,
 *		  destroying any previous allocations.  Commonly
 *		  called after zinitPool() to make a pool available
 *		  for use.
 */

void
zclearPool(MemPool *mp)
{
	MemNode *mn = mp->mp_Base;

	mn->mr_Next = NULL;
	mn->mr_Bytes = mp->mp_Size;
	mp->mp_First = mn;
	mp->mp_Used = 0;
}

#ifdef ZALLOCDEBUG

void
zallocstats(MemPool *mp)
{
	int abytes = 0;
	int hbytes = 0;
	int fcount = 0;
	MemNode *mn;

//	fprintf(stderr, "Pool %s, %d bytes reserved", mp->mp_Ident, mp->mp_Size);

	mn = mp->mp_First;

	if ((void *)mn != (void *)mp->mp_Base) {
		abytes += (char *)mn - (char *)mp->mp_Base;
	}

	while (mn) {
		if ((char *)mn + mn->mr_Bytes != mp->mp_End) {
			hbytes += mn->mr_Bytes;
			++fcount;
		}
		if (mn->mr_Next)
			abytes += (char *)mn->mr_Next - ((char *)mn + mn->mr_Bytes);
		mn = mn->mr_Next;
	}
/*	fprintf(stderr, " %d bytes allocated\n%d fragments (%d bytes fragmented)\n",
			abytes,
			fcount,
			hbytes
	); */
//	fflush(stderr);
}

#endif

