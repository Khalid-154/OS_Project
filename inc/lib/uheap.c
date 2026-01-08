#include <inc/lib.h>
#include <inc/queue.h>

#define MAX_USER_CHUNKS 4000

struct UserHeapChunk
{
	uint32 startVA;
	uint32 size;
	bool isFree;

	LIST_ENTRY(UserHeapChunk)prev_next_info;
	};

LIST_HEAD(UserChunksList,UserHeapChunk);
static struct UserChunksList UserChunks;


//uint32 uheapPageAllocStart=0;
//uint32 uheapPageAllocBreak =0;
//uint32 uheapPlaceStategy =0;


static struct UserHeapChunk chunkspool[MAX_USER_CHUNKS];
static int next_chunk_index=0;

static struct UserHeapChunk* allocChunk()
{
	if(next_chunk_index>=MAX_USER_CHUNKS)
	{
		return NULL;
	}
	return &chunkspool[next_chunk_index++];
}

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//

//==============================================
// [1] INITIALIZE USER HEAP:
//==============================================
int __firstTimeFlag = 1;
void uheap_init()
{
	if(__firstTimeFlag)
	{
		initialize_dynamic_allocator(USER_HEAP_START, USER_HEAP_START + DYN_ALLOC_MAX_SIZE);
		uheapPlaceStrategy = sys_get_uheap_strategy();
		uheapPageAllocStart = dynAllocEnd + PAGE_SIZE;
		uheapPageAllocBreak = uheapPageAllocStart;
		LIST_INIT(&UserChunks);
		next_chunk_index=0;

		__firstTimeFlag = 0;
	}
}

//==============================================
// [2] GET A PAGE FROM THE KERNEL FOR DA:
//==============================================
int get_page(void* va)
{
	int ret = __sys_allocate_page(ROUNDDOWN(va, PAGE_SIZE), PERM_USER|PERM_WRITEABLE|PERM_UHPAGE);
	if (ret < 0)
		panic("get_page() in user: failed to allocate page from the kernel");
	return 0;
}

//==============================================
// [3] RETURN A PAGE FROM THE DA TO KERNEL:
//==============================================
void return_page(void* va)
{
	int ret = __sys_unmap_frame(ROUNDDOWN((uint32)va, PAGE_SIZE));
	if (ret < 0)
		panic("return_page() in user: failed to return a page to the kernel");
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//=================================
// [1] ALLOCATE SPACE IN USER HEAP:
//=================================
void* malloc(uint32 size)
{
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	if (size == 0) return NULL ;
	//==============================================================
	//TODO: [PROJECT'25.IM#2] USER HEAP - #1 malloc
	//Your code is here
	uint32 alloc_size=ROUNDUP(size,PAGE_SIZE);
	struct UserHeapChunk* ch;
	struct UserHeapChunk* best=NULL;//worst fit
	struct UserHeapChunk* same=NULL;//exact fit

		LIST_FOREACH(ch,&UserChunks)
		{
			if(ch->isFree)
			{
				if(ch->size==alloc_size)
				{
					same=ch;
					break;
				}

				if(ch->size > alloc_size)
				{
					if(best==NULL || ch->size > best->size)
					{
						best=ch;
					}
				}
			}
		}

		uint32 va=0;

		if(same!=NULL)
		{
			same->isFree=0;
			va=same->startVA;
		}
		else if(best!=NULL)
		{
			struct UserHeapChunk* rest=allocChunk();
			if(rest==NULL)
			{
				return NULL;
			}

			rest->startVA=best->startVA + alloc_size;
			rest->size=best->size - alloc_size;
			rest->isFree=1;

			LIST_INSERT_AFTER(&UserChunks,best,rest);

			best->size=alloc_size;
			best->isFree=0;

			va=best->startVA;
		}
		else
		{
			if (alloc_size > USER_HEAP_MAX - uheapPageAllocBreak)
			{
				return NULL;
			}

			struct UserHeapChunk *nn=allocChunk();
			if(nn==NULL)
			{
				return NULL;
			}
			nn->startVA=uheapPageAllocBreak;
			nn->size=alloc_size;
			nn->isFree=0;

			LIST_INSERT_TAIL(&UserChunks,nn);

			va=nn->startVA;
			uheapPageAllocBreak = uheapPageAllocBreak + alloc_size;
		}

		sys_allocate_user_mem(va,alloc_size);

		return (void*)va;


	//Comment the following line
	//panic("malloc() is not implemented yet...!!");
}

//=================================
// [2] FREE SPACE FROM USER HEAP:
//=================================
void free(void* virtual_address)
{
	//TODO: [PROJECT'25.IM#2] USER HEAP - #3 free
	//Your code is here
	uint32 va = (uint32)virtual_address;
	if (va >= USER_HEAP_START && va < (USER_HEAP_START + DYN_ALLOC_MAX_SIZE))
	{
		free_block(virtual_address);
		return;
	}

	struct UserHeapChunk* ch;
	struct UserHeapChunk* it;
	LIST_FOREACH(ch,&UserChunks)
	{
		if(ch->startVA==va)
		{
			if(ch->isFree)
			{
				return;
			}
			ch->isFree=1;

			sys_free_user_mem(ch->startVA,ch->size);

			struct UserHeapChunk* next=LIST_NEXT(ch);
			if(next!= NULL && next->isFree)
			{
				ch->size+=next->size;
				LIST_REMOVE(&UserChunks,next);
			}

			struct UserHeapChunk* prev=NULL;
			LIST_FOREACH(it,&UserChunks)
			{
				if(LIST_NEXT(it)==ch)
				{
					prev=it;
					break;
				}
			}

			if(prev !=NULL && prev->isFree)
			{
				prev->size+=ch->size;
				LIST_REMOVE(&UserChunks,ch);
				ch=prev;
			}

			struct UserHeapChunk* last=NULL;
			LIST_FOREACH(it,&UserChunks)
			{
				last=it;
			}
			if(last==ch && ch->isFree)
			{
				uheapPageAllocBreak=ch->startVA;
				LIST_REMOVE(&UserChunks,ch);
			}

			return;

		}
	}

			//panic("invalid address");

	//Comment the following line
	//panic("free() is not implemented yet...!!");
}

//=================================
// [3] ALLOCATE SHARED VARIABLE:
//=================================
void* smalloc(char *sharedVarName, uint32 size, uint8 isWritable)
{
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	if (size == 0) return NULL ;
	//==============================================================

	//TODO: [PROJECT'25.IM#3] SHARED MEMORY - #2 smalloc
	//Your code is here
	uint32 alloc_size=ROUNDUP(size,PAGE_SIZE);

	struct UserHeapChunk* ch;
	struct UserHeapChunk* best=NULL;//worst fit
	struct UserHeapChunk* same=NULL;//exact fit

	LIST_FOREACH(ch,&UserChunks)
	{
		if(ch->isFree)
		{
			if(ch->size==alloc_size)
			{
				same=ch;
				break;
			}

			if(ch->size > alloc_size)
			{
				if(best==NULL || ch->size > best->size)
				{
					best=ch;
				}
			}
		}
	}

	uint32 va=0;

	if(same!=NULL)
	{
		same->isFree=0;
		va=same->startVA;
	}
	else if(best!=NULL)
	{
		struct UserHeapChunk* rest=allocChunk();
		if(rest==NULL)
		{
			return NULL;
		}

		rest->startVA=best->startVA + alloc_size;
		rest->size=best->size - alloc_size;
		rest->isFree=1;

		LIST_INSERT_AFTER(&UserChunks,best,rest);

		best->size=alloc_size;
		best->isFree=0;

		va=best->startVA;
	}
	else
	{
		if (alloc_size > USER_HEAP_MAX - uheapPageAllocBreak)
		{
			return NULL;
		}

		struct UserHeapChunk *nn=allocChunk();
		if(nn==NULL)
		{
			return NULL;
		}
		nn->startVA=uheapPageAllocBreak;
		nn->size=alloc_size;
		nn->isFree=0;

		LIST_INSERT_TAIL(&UserChunks,nn);

		va=nn->startVA;
		uheapPageAllocBreak = uheapPageAllocBreak + alloc_size;
	}

	int id=sys_create_shared_object(sharedVarName,size,isWritable,(void*)va);
	if(id<0)
	{
		LIST_FOREACH(ch,&UserChunks)
		{
			if(ch->startVA==va)
			{
				ch->isFree=1;
				break;
			}
		}
		return NULL;
	}

	return (void*)va;
	//Comment the following line
	//panic("smalloc() is not implemented yet...!!");
}

//========================================
// [4] SHARE ON ALLOCATED SHARED VARIABLE:
//========================================
void* sget(int32 ownerEnvID, char *sharedVarName)
{
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	//==============================================================

	//TODO: [PROJECT'25.IM#3] SHARED MEMORY - #4 sget
	//Your code is here
	int size=sys_size_of_shared_object(ownerEnvID,sharedVarName);
	if(size<0)
	{
		return NULL;
	}

	uint32 alloc_size=ROUNDUP(size,PAGE_SIZE);

		struct UserHeapChunk* ch;
		struct UserHeapChunk* best=NULL;//worst fit
		struct UserHeapChunk* same=NULL;//exact fit

		LIST_FOREACH(ch,&UserChunks)
		{
			if(ch->isFree)
			{
				if(ch->size==alloc_size)
				{
					same=ch;
					break;
				}

				if(ch->size > alloc_size)
				{
					if(best==NULL || ch->size > best->size)
					{
						best=ch;
					}
				}
			}
		}


		uint32 va=0;

			if(same!=NULL)
			{
				same->isFree=0;
				va=same->startVA;
			}
			else if(best!=NULL)
			{
				struct UserHeapChunk* rest=allocChunk();
				if(rest==NULL)
				{
					return NULL;
				}

				rest->startVA=best->startVA + alloc_size;
				rest->size=best->size - alloc_size;
				rest->isFree=1;

				LIST_INSERT_AFTER(&UserChunks,best,rest);

				best->size=alloc_size;
				best->isFree=0;

				va=best->startVA;
			}
			else
			{
				if (alloc_size > USER_HEAP_MAX - uheapPageAllocBreak)
				{
					return NULL;
				}

				struct UserHeapChunk *nn=allocChunk();
				if(nn==NULL)
				{
					return NULL;
				}
				nn->startVA=uheapPageAllocBreak;
				nn->size=alloc_size;
				nn->isFree=0;

				LIST_INSERT_TAIL(&UserChunks,nn);

				va=nn->startVA;
				uheapPageAllocBreak = uheapPageAllocBreak + alloc_size;
			}

			int id=sys_get_shared_object(ownerEnvID,sharedVarName,(void*)va);

			if(id<0)
			{
				LIST_FOREACH(ch,&UserChunks)
				{
					if(ch->startVA==va)
					{
						ch->isFree=1;
						break;
					}
				}
				return NULL;
			}

			return (void*)va;
	//Comment the following line
	//panic("sget() is not implemented yet...!!");
}


//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//


//=================================
// REALLOC USER SPACE:
//=================================
//	Attempts to resize the allocated space at "virtual_address" to "new_size" bytes,
//	possibly moving it in the heap.
//	If successful, returns the new virtual_address, in which case the old virtual_address must no longer be accessed.
//	On failure, returns a null pointer, and the old virtual_address remains valid.

//	A call with virtual_address = null is equivalent to malloc().
//	A call with new_size = zero is equivalent to free().

//  Hint: you may need to use the sys_move_user_mem(...)
//		which switches to the kernel mode, calls move_user_mem(...)
//		in "kern/mem/chunk_operations.c", then switch back to the user mode here
//	the move_user_mem() function is empty, make sure to implement it.
void *realloc(void *virtual_address, uint32 new_size)
{
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	//==============================================================
	panic("realloc() is not implemented yet...!!");
}


//=================================
// FREE SHARED VARIABLE:
//=================================
//	This function frees the shared variable at the given virtual_address
//	To do this, we need to switch to the kernel, free the pages AND "EMPTY" PAGE TABLES
//	from main memory then switch back to the user again.
//
//	use sys_delete_shared_object(...); which switches to the kernel mode,
//	calls delete_shared_object(...) in "shared_memory_manager.c", then switch back to the user mode here
//	the delete_shared_object() function is empty, make sure to implement it.
void sfree(void* virtual_address)
{
	//TODO: [PROJECT'25.BONUS#5] EXIT #2 - sfree
	//Your code is here
	//Comment the following line
	panic("sfree() is not implemented yet...!!");

	//	1) you should find the ID of the shared variable at the given address
	//	2) you need to call sys_freeSharedObject()
}


//==================================================================================//
//========================== MODIFICATION FUNCTIONS ================================//
//==================================================================================//
