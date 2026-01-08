#include "kheap.h"
#include <inc/queue.h>
#include <inc/memlayout.h>
#include <inc/dynamic_allocator.h>
#include <kern/conc/sleeplock.h>
#include <kern/proc/user_environment.h>
#include <kern/mem/memory_manager.h>
#include "../conc/kspinlock.h"

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//

//==============================================
// [1] INITIALIZE KERNEL HEAP:
//==============================================
//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #0 kheap_init [GIVEN]
//Remember to initialize locks (if any)
void kheap_init() {
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		initialize_dynamic_allocator(KERNEL_HEAP_START,
				KERNEL_HEAP_START + DYN_ALLOC_MAX_SIZE);
		set_kheap_strategy(KHP_PLACE_CUSTOMFIT);
		kheapPageAllocStart = dynAllocEnd + PAGE_SIZE;
		kheapPageAllocBreak = kheapPageAllocStart;
		LIST_INIT(&Page_Alloc_Chunks);
	}
	//==================================================================================
	//==================================================================================
}

//==============================================
// [2] GET A PAGE FROM THE KERNEL FOR DA:
//==============================================
int get_page(void* va) {
	uint32 page_va = ROUNDDOWN((uint32 )va, PAGE_SIZE);
	int ret = alloc_page(ptr_page_directory, ROUNDDOWN((uint32 )va, PAGE_SIZE),
			PERM_WRITEABLE, 1);
	if (ret < 0)
		panic("get_page() in kern: failed to allocate page from the kernel");
	uint32 page_pa = kheap_physical_address((unsigned int) page_va);
	struct FrameInfo* frame = to_frame_info(page_pa);
	frame->frame_virt_addr = page_va;
	return 0;
}

//==============================================
// [3] RETURN A PAGE FROM THE DA TO KERNEL:
//==============================================
void return_page(void* va) {
	unmap_frame(ptr_page_directory, ROUNDDOWN((uint32 )va, PAGE_SIZE));
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//
//===================================
// [1] ALLOCATE SPACE IN KERNEL HEAP:
//===================================
int number_of_free_chunks = 0;
static struct PageAllocChunk CHUNKS[(KERNEL_HEAP_START - KERNEL_HEAP_MAX)
		/ PAGE_SIZE]; /////////////////////// i geuss it needs locks

static int CHUNK_INDEX = 0;
void* kmalloc(unsigned int size) {
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #1 kmalloc
	//kpanic_into_prompt("kmalloc() is not implemented yet...!!");
	/* =========================================================================== */
	/* ================================== CASE_1 ================================= */
	/* =================== size<=2KB uses alloc_block in DA======================= */
	if (size <= DYN_ALLOC_MAX_BLOCK_SIZE)
		return alloc_block(size);

	uint32 alloced_size = ROUNDUP((uint32 )size, PAGE_SIZE);
	int pages_to_alloc = alloced_size / PAGE_SIZE;

	// save the exact fit chunk & largest chunk for (worst fit)
	//**************************************************************************************************
	struct PageAllocChunk *EXACT_FIT_Chunk = NULL;
	struct PageAllocChunk *WORST_FIT_Chunk = NULL;
	struct PageAllocChunk *Chunk = NULL;
	// HERE WILL MAKE IF CONDITION WILL SPEED UP))(IF Page_Alloc_Chunks.HAS_ANY_FREE BLOCK)
	if (number_of_free_chunks != 0) {
		LIST_FOREACH(Chunk,&Page_Alloc_Chunks)
		{
			if (Chunk->is_free) {
				if (Chunk->Size_limit == alloced_size) {
					EXACT_FIT_Chunk = Chunk;
					break;
				}
				if (WORST_FIT_Chunk == NULL
						|| Chunk->Size_limit > WORST_FIT_Chunk->Size_limit)
					WORST_FIT_Chunk = Chunk; // means bigger chunk
			}
		}
	}
	//***************************************************************************************************
	/* =========================================================================== */
	/* ================================== CASE_2 ================================= */
	/* ================================ EXCAT FIT ================================ */
	if (EXACT_FIT_Chunk != NULL) { // found exact chunk with equal size needed
		for (int i = 0; i < pages_to_alloc; i++) {
			get_page(
					(void*) ((uint32) (EXACT_FIT_Chunk->st_Va
							+ (uint32) i * PAGE_SIZE)));
		}
		EXACT_FIT_Chunk->is_free = 0;
		EXACT_FIT_Chunk->num_pages = pages_to_alloc;
		number_of_free_chunks--;
		return (void*) (uint32) EXACT_FIT_Chunk->st_Va;
	}
	/* =========================================================================== */
	/* ================================== CASE_3 ================================= */
	/* ================================ WORST FIT ================================ */
	if (WORST_FIT_Chunk != NULL && WORST_FIT_Chunk->Size_limit > alloced_size) {
		uint32 OLDChunkSize = WORST_FIT_Chunk->Size_limit;
		int OLDChunkPagesNum = WORST_FIT_Chunk->num_pages;

		struct PageAllocChunk *splitted_Chunk = &CHUNKS[CHUNK_INDEX++];
		if (splitted_Chunk == NULL) {
			cprintf("null splitted_Chunk pointer");
			return NULL;
		}
		splitted_Chunk->Size_limit = OLDChunkSize - alloced_size;
		splitted_Chunk->is_free = 1;
		splitted_Chunk->num_pages = OLDChunkPagesNum - pages_to_alloc;
		splitted_Chunk->st_Va =
				(uint32) (WORST_FIT_Chunk->st_Va + alloced_size);
		LIST_INSERT_AFTER(&Page_Alloc_Chunks,WORST_FIT_Chunk, splitted_Chunk);

		for (int i = 0; i < pages_to_alloc; i++) {
			get_page(
					(void*) ((uint32) (WORST_FIT_Chunk->st_Va
							+ (uint32) i * PAGE_SIZE)));
		}

		WORST_FIT_Chunk->Size_limit = alloced_size;
		WORST_FIT_Chunk->is_free = 0;
		WORST_FIT_Chunk->num_pages = pages_to_alloc;

		return (void*) (uint32) (WORST_FIT_Chunk->st_Va);
	}
	/* =========================================================================== */
	/* ================================== CASE_4 ================================= */
	/* ================== TAKE FROM UNUSED AREA (MOVE BREAK UP) ================== */

	/* ========================== CASE_5 (NO ENOUGH MEMORY) ========================== */
	if (KERNEL_HEAP_MAX - kheapPageAllocBreak < alloced_size) {
		return NULL;
	}
	struct PageAllocChunk *NEWchunk = &CHUNKS[CHUNK_INDEX++];

	NEWchunk->st_Va = kheapPageAllocBreak;
	NEWchunk->is_free = 0;
	NEWchunk->num_pages = pages_to_alloc;
	NEWchunk->Size_limit = alloced_size;
	kheapPageAllocBreak += NEWchunk->Size_limit;
	LIST_INSERT_TAIL(&Page_Alloc_Chunks, NEWchunk);

	for (int i = 0; i < pages_to_alloc; i++) {
		get_page((void*) ((uint32) (NEWchunk->st_Va + (uint32) i * PAGE_SIZE)));
	}
	return (void*) (uint32) (NEWchunk->st_Va);
}

//=================================
// [2] FREE SPACE FROM KERNEL HEAP:
//=================================
void kfree(void* virtual_address) {
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #2 kfree
	//panic("kfree() is not implemented yet...!!");
	/* ================================== Kfree block allocator ================================= */
	uint32 va = (uint32) virtual_address;
	if (va >= dynAllocStart && va < dynAllocEnd) {
		free_block(virtual_address);
		return;
	}
	/* ================================== Kfr 	ee page allocator ================================= */
	if (va >= kheapPageAllocStart && va < kheapPageAllocBreak) {
		struct PageAllocChunk *Chunk = NULL;

		LIST_FOREACH(Chunk,&Page_Alloc_Chunks)
		{
			if (Chunk->st_Va == (uint32) virtual_address) {	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
				Chunk->is_free = 1;
				number_of_free_chunks++;
				for (int i = 0; i < Chunk->num_pages; i++) {
					return_page((void*) Chunk->st_Va + (uint32) i * PAGE_SIZE);
				}

				// untill now you freed the desired chunk (check for prev,next chunks)
				struct PageAllocChunk *prevChunk = LIST_PREV(Chunk);
				struct PageAllocChunk *nextChunk = LIST_NEXT(Chunk);

				// if brevieos chunk is free
				if (prevChunk != NULL && prevChunk->is_free) {

					for (int i = 0; i < prevChunk->num_pages; i++) {
						return_page(
								(void*) prevChunk->st_Va
										+ (uint32) i * PAGE_SIZE);
					}
					Chunk->st_Va = prevChunk->st_Va;
					Chunk->Size_limit += prevChunk->Size_limit;
					Chunk->num_pages += prevChunk->num_pages;
					LIST_REMOVE(&Page_Alloc_Chunks, prevChunk);
					number_of_free_chunks--;
				}

				if (nextChunk != NULL && nextChunk->is_free) {

					for (int i = 0; i < nextChunk->num_pages; i++) {
						return_page(
								(void*) nextChunk->st_Va
										+ (uint32) i * PAGE_SIZE);
					}
					Chunk->Size_limit += nextChunk->Size_limit;
					Chunk->num_pages += nextChunk->num_pages;
					LIST_REMOVE(&Page_Alloc_Chunks, nextChunk);
					number_of_free_chunks--;
				}
				struct PageAllocChunk *lastchunk = LIST_LAST(
						&Page_Alloc_Chunks);
				if (lastchunk == Chunk) {
					kheapPageAllocBreak -= Chunk->Size_limit;
					LIST_REMOVE(&Page_Alloc_Chunks, Chunk);
					number_of_free_chunks--;
					return;
				}
			}///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		}
	}
}

//=================================
//=================================
// [3] FIND VA OF GIVEN PA:
//=================================
unsigned int kheap_virtual_address(unsigned int physical_address) {
	// HERE I MADE A POWERFULL IDEA to make it o(1)(uosef mohamed (2023170738))
	// Editing in FrameInfo Struct (added member"frame_virt_addr")
	// to save and keep track for page virtual address
	// update (get_page (set value frame_virt_addr)) & (return_page (del frame_virt_addr))
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #3 kheap_virtual_address
	struct FrameInfo* Frame_info = to_frame_info((uint32) physical_address);
	uint32 pa_page = physical_address & 0xFFFFF000;
	uint32 pa_offset = physical_address & 0x00000FFF;

	if (Frame_info->frame_virt_addr == 0)
		return 0;
	return (unsigned int) (Frame_info->frame_virt_addr + pa_offset);
}

//=================================
// [4] FIND PA OF GIVEN VA:
//=================================
unsigned int kheap_physical_address(unsigned int virtual_address) {
	uint32* ptr_page_table;
	int ret = get_page_table(ptr_page_directory, (uint32) virtual_address,
			&ptr_page_table);

	if (ret == TABLE_IN_MEMORY && ptr_page_table != NULL) {
		uint32 entry = ptr_page_table[PTX(virtual_address)];
		if (entry & PERM_PRESENT) {
			uint32 pa = (entry & 0xFFFFF000) | (virtual_address & 0xFFF);
			return (unsigned int) pa;
		} else {
			return 0; // table exists but page not present
		}
	}

	/* If VA is in the static kernel direct mapped region, translate using static macro */
	if (virtual_address >= KERNEL_BASE) {
		return (unsigned int) STATIC_KERNEL_PHYSICAL_ADDRESS(virtual_address);
	}

	return 0;
}

//=================================================================================//
//============================== BONUS FUNCTION ===================================//
//=================================================================================//
// krealloc():

//	Attempts to resize the allocated space at "virtual_address" to "new_size" bytes,
//	possibly moving it in the heap.
//	If successful, returns the new virtual_address, in which case the old virtual_address must no longer be accessed.
//	On failure, returns a null pointer, and the old virtual_address remains valid.

//	A call with virtual_address = null is equivalent to kmalloc().
//	A call with new_size = zero is equivalent to kfree().

extern __inline__ uint32 get_block_size(void *va);

void *krealloc(void *virtual_address, uint32 new_size) {
	//TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - krealloc
	/*uint32 va = (uint32) virtual_address;
	if (virtual_address == NULL)
		return kmalloc(new_size);
	if (new_size == 0) {
		kfree(virtual_address);
		return NULL;
	}
	if (va >= dynAllocStart && va < dynAllocEnd) { //we have block allocator

		uint32 old_size = get_block_size(virtual_address);
		struct BlockElement *NewBlk = alloc_block(new_size);
		// memcpy(void *dst, const void *src, uint32 n)
		memcpy((void*) NewBlk, virtual_address, old_size);
		free_block(virtual_address);
		return NewBlk;
	} else if (va >= KERNEL_HEAP_START && va < KERNEL_HEAP_MAX) { // we have page alocator
		struct PageAllocChunk *Chunk = NULL;
		LIST_FOREACH(Chunk,&Page_Alloc_Chunks)
		{
			struct PageAllocChunk *prevChunk = LIST_PREV(Chunk);
			struct PageAllocChunk *nextChunk = LIST_NEXT(Chunk);
			if (Chunk->st_Va == (uint32) virtual_address) {
				if (nextChunk != NULL && nextChunk->is_free) {
					if ((Chunk->Size_limit + nextChunk->Size_limit)
							> new_size) { // there is space upove to expand

					}

				}
			}

		}

	}
	*/
	panic("krealloc() is not implemented yet...!!");
}
