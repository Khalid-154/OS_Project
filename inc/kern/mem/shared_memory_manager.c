#include <inc/memlayout.h>
#include "shared_memory_manager.h"

#include <inc/mmu.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/queue.h>
#include <inc/environment_definitions.h>

#include <kern/proc/user_environment.h>
#include <kern/trap/syscall.h>
#include "kheap.h"
#include "memory_manager.h"

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//

//===========================
// [1] INITIALIZE SHARES:
//===========================
//Initialize the list and the corresponding lock
void sharing_init() {
#if USE_KHEAP
	LIST_INIT(&AllShares.shares_list)
	;
	init_kspinlock(&AllShares.shareslock, "shares lock");
#else
	panic("not handled when KERN HEAP is disabled");
#endif
}

//=========================
// [2] Find Share Object:
//=========================
//Search for the given shared object in the "shares_list"
//Return:
//	a) if found: ptr to Share object
//	b) else: NULL
struct Share* find_share(int32 ownerID, char* name) {
#if USE_KHEAP
	struct Share * ret = NULL;
	bool wasHeld = holding_kspinlock(&(AllShares.shareslock));
	if (!wasHeld) {
		acquire_kspinlock(&(AllShares.shareslock));
	}
	{
		struct Share * shr;
		LIST_FOREACH(shr, &(AllShares.shares_list))
		{
			if (shr->ownerID == ownerID && strcmp(name, shr->name) == 0) {
				ret = shr;
				break;
			}
		}
	}
	if (!wasHeld) {
		release_kspinlock(&(AllShares.shareslock));
	}
	return ret;
#else
	panic("not handled when KERN HEAP is disabled");
#endif
}

//==============================
// [3] Get Size of Share Object:
//==============================
int size_of_shared_object(int32 ownerID, char* shareName) {
	struct Share* ptr_share = find_share(ownerID, shareName);
	if (ptr_share == NULL)
		return E_SHARED_MEM_NOT_EXISTS;
	else
		return ptr_share->size;

	return 0;
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//=====================================
// [1] Alloc & Initialize Share Object:
//=====================================
struct Share* alloc_share(int32 ownerID, char* shareName, uint32 size,
		uint8 isWritable) {
	struct Share* sh = kmalloc(sizeof(struct Share));
	if (sh == NULL) {
		return NULL;
	}
	int i = 0;
	while (i < 63 && shareName[i] != 0) {
		sh->name[i] = shareName[i];
		i++;
	}
	sh->name[i] = 0;

	sh->ownerID = ownerID;
	sh->size = size;
	sh->isWritable = isWritable;
	sh->references = 1;

	uint32 pages = ROUNDUP(size,PAGE_SIZE) / PAGE_SIZE;
	if (pages == 0) {
		pages = 1;
	}
	sh->framesStorage = kmalloc(pages * sizeof(struct FrameInfo*));
	if (sh->framesStorage == NULL) {
		kfree(sh);
		return NULL;
	}

	for (uint32 k = 0; k < pages; k++) {
		sh->framesStorage[k] = NULL;
	}

	sh->ID = ((uint32) sh) & 0x7FFFFFFF;

	return sh;
}

//=========================
// [4] Create Share Object:
//=========================
int create_shared_object(int32 ownerID, char* shareName, uint32 size,
		uint8 isWritable, void* virtual_address) {
#if USE_KHEAP

	struct Env* myenv = get_cpu_proc();

	acquire_kspinlock(&AllShares.shareslock);
	struct Share* s;
	LIST_FOREACH(s,&AllShares.shares_list)
	{
		if (s->ownerID == ownerID && strcmp(s->name, shareName) == 0) {
			release_kspinlock(&AllShares.shareslock);
			return E_SHARED_MEM_EXISTS;
		}
	}
	release_kspinlock(&AllShares.shareslock);

	struct Share* sh = alloc_share(ownerID, shareName, size, isWritable);
	if (sh == NULL) {
		return E_NO_SHARE;
	}

	uint32 pages = ROUNDUP(size,PAGE_SIZE) / PAGE_SIZE;
	if (pages == 0) {
		pages = 1;
	}

	uint32 baseVA = (uint32) virtual_address;
	int perms = PERM_USER | PERM_WRITEABLE;
	if (isWritable)
		perms = perms | PERM_WRITEABLE;

	for (uint32 i = 0; i < pages; i++) {
		struct FrameInfo* f = NULL;
		if (allocate_frame(&f) != 0 || f == NULL)
			return E_NO_SHARE;

		if (map_frame(myenv->env_page_directory, f, baseVA + i * PAGE_SIZE,
				perms) != 0) {
			free_frame(f);
			return E_NO_SHARE;
		}

		sh->framesStorage[i] = f;
	}

	acquire_kspinlock(&AllShares.shareslock);
	LIST_INSERT_TAIL(&AllShares.shares_list, sh);
	release_kspinlock(&AllShares.shareslock);

	return sh->ID;
#else
	panic("USE KHEAP SHOULD BE 1");
#endif
}

//======================
// [5] Get Share Object:
//======================
int get_shared_object(int32 ownerID, char* shareName, void* virtual_address) {
#if USE_KHEAP
	struct Env* myenv = get_cpu_proc();
	acquire_kspinlock(&AllShares.shareslock);
	struct Share* sh = find_share(ownerID, shareName);
	if (sh == NULL) {
		release_kspinlock(&AllShares.shareslock);
		return E_SHARED_MEM_NOT_EXISTS;
	}

	uint32 size = sh->size;
	uint32 np = ROUNDUP(size,PAGE_SIZE) / PAGE_SIZE;
	if (np == 0)
		np = 1;

	sh->references++;
	struct FrameInfo** frames = sh->framesStorage;
	release_kspinlock(&AllShares.shareslock);

	uint32 baseVA = (uint32) virtual_address;
	int perms = PERM_USER;
	if (sh->isWritable)
		perms = perms | PERM_WRITEABLE;

	for (uint32 i = 0; i < np; i++)
		if (map_frame(myenv->env_page_directory, frames[i],
				baseVA + i * PAGE_SIZE, perms) != 0)
			return E_SHARED_MEM_NOT_EXISTS;

	return sh->ID;
#else
	panic("USE KHEAP SHOULD BE 1");
#endif
}

//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//
void free_share(struct Share* ptrShare) {
	panic("free_share() is not implemented yet...!!");
}

int delete_shared_object(int32 sharedObjectID, void *startVA) {
	panic("delete_shared_object() is not implemented yet...!!");
	return 0;
}
