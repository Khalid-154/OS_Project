#include <inc/assert.h>
#include <inc/string.h>
#include "../inc/dynamic_allocator.h"

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
//==================================
//==================================
// [1] GET PAGE VA:
//==================================
__inline__ uint32 to_page_va(struct PageInfoElement *ptrPageInfo) {
	if (ptrPageInfo < &pageBlockInfoArr[0]
			|| ptrPageInfo >= &pageBlockInfoArr[DYN_ALLOC_MAX_SIZE / PAGE_SIZE])
		panic("to_page_va called with invalid pageInfoPtr");
	//Get start VA of the page from the corresponding Page Info pointer
	int idxInPageInfoArr = (ptrPageInfo - pageBlockInfoArr);
	return dynAllocStart + (idxInPageInfoArr << PGSHIFT);
}

//==================================
// [2] GET PAGE INFO OF PAGE VA:
//==================================
__inline__ struct PageInfoElement * to_page_info(uint32 va) {
	int idxInPageInfoArr = (va - dynAllocStart) >> PGSHIFT;
	if (idxInPageInfoArr < 0|| idxInPageInfoArr >= DYN_ALLOC_MAX_SIZE/PAGE_SIZE)
		panic("to_page_info called with invalid pa");
	return &pageBlockInfoArr[idxInPageInfoArr];
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//==================================
// [1] INITIALIZE DYNAMIC ALLOCATOR:
//==================================
bool is_initialized = 0;
void initialize_dynamic_allocator(uint32 daStart, uint32 daEnd) {
	{
		assert(daEnd <= daStart + DYN_ALLOC_MAX_SIZE);
		is_initialized = 1;
	}
	dynAllocStart = daStart;
	dynAllocEnd = daEnd;

	for (int i = 0; i <= LOG2_MAX_SIZE - LOG2_MIN_SIZE; i++) {
		LIST_INIT(&freeBlockLists[i]);
	}

	LIST_INIT(&freePagesList);

	int numPages = (daEnd - daStart) / PAGE_SIZE;

	for (int i = 0; i < numPages; i++) {
		pageBlockInfoArr[i].block_size = 0;
		pageBlockInfoArr[i].num_of_free_blocks = 0;

		LIST_INSERT_TAIL(&freePagesList, &pageBlockInfoArr[i]);
	}

}

//===========================
// [2] GET BLOCK SIZE:
//===========================
__inline__ uint32 get_block_size(void *va) {
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #2 get_block_size

	// Get the PageInfo  that contain this va
	struct PageInfoElement *page = to_page_info((uint32) va);

	// Get the block size of this page
	uint32 size = page->block_size;

	return size;

	//panic("get_block_size() Not implemented yet");
}

//===========================
// 3) ALLOCATE BLOCK:
//===========================
void get_size_and_index(uint32 size, uint32* alloc_size, int* index) {
	if (size <= 8) {
		*alloc_size = 8;
		*index = 0;
	} else if (size > 8 && size <= 16) {
		*alloc_size = 16;
		*index = 1;
	} else if (size > 16 && size <= 32) {
		*alloc_size = 32;
		*index = 2;
	} else if (size > 32 && size <= 64) {
		*alloc_size = 64;
		*index = 3;
	} else if (size > 64 && size <= 128) {
		*alloc_size = 128;
		*index = 4;
	} else if (size > 128 && size <= 256) {
		*alloc_size = 256;
		*index = 5;
	} else if (size > 256 && size <= 512) {
		*alloc_size = 512;
		*index = 6;
	} else if (size > 512 && size <= 1024) {
		*alloc_size = 1024;
		*index = 7;
	} else if (size > 1024 && size <= 2048) {
		*alloc_size = 2048;
		*index = 8;
	}
}

void *alloc_block(uint32 size) {
	{
		assert(size <= DYN_ALLOC_MAX_BLOCK_SIZE);
	}

	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #3 alloc_block
	uint32 alloc_size = 0;
	int index = 0;
	struct BlockElement *block = NULL;
	struct PageInfoElement *pageInfo;
	if (size == 0)
		return NULL;

	// Getting allocated Size & index in freeBlockLists
	get_size_and_index(size, &alloc_size, &index);
	//cprintf("size : %d and index : %d",alloc_size,index);

	// If there is a free block in this category of sizes
	/* =========================================================================== */
	/* ================================== CASE_1 ================================= */
	/* =========================================================================== */
	if (!LIST_EMPTY(&freeBlockLists[index])) {
		//cprintf(" hi from case 1");
		// get last block, update DS(page info elemnt , freeBlockLists[index])
		block = LIST_LAST(&freeBlockLists[index]);
		pageInfo = to_page_info((uint32) block);
		pageInfo->num_of_free_blocks--;
		LIST_REMOVE(&freeBlockLists[index], block);
		return (void*) block;
	}
	// No any block with desired size
	/* =========================================================================== */
	/* ================================== CASE_2 ================================= */
	/* =========================================================================== */
	else if (!LIST_EMPTY(&freePagesList)) {
		//  cprintf(" hi from case 2 after cond");
		// get first free page , set its size & free , update DS (freePagesList)
		struct PageInfoElement *NEW_pageInfo;
		NEW_pageInfo = LIST_FIRST(&freePagesList);
		uint32 pageInfo_Va = to_page_va(NEW_pageInfo);
		//  cprintf(" hi befor getpage in case 2");
		get_page((void*) pageInfo_Va);
		// cprintf(" hi after getpage in case 2");
		NEW_pageInfo->block_size = alloc_size;
		NEW_pageInfo->num_of_free_blocks = PAGE_SIZE / alloc_size;
		LIST_REMOVE(&freePagesList, NEW_pageInfo);

		// Add all blocks in freeBlockLists[]
		uint32 page_va = to_page_va(NEW_pageInfo);
		for (uint32 i = 0; i < PAGE_SIZE; i += alloc_size) {
			struct BlockElement *blk = (struct BlockElement *) (page_va + i);
			LIST_INSERT_TAIL(&freeBlockLists[index], blk);
		}

		// get a block from freeBlockLists[], update DS (freeBlockLists,pageInfo)
		block = LIST_LAST(&freeBlockLists[index]);
		LIST_REMOVE(&freeBlockLists[index], block);
		pageInfo = to_page_info((uint32) block);
		pageInfo->num_of_free_blocks--;
		return (void*) block;

	}
	// No any free pages left
	/* =========================================================================== */
	/* ================================== CASE_3 ================================= */
	/* =========================================================================== */
	else {
		// search for higher size level
		for (int i = index + 1; i <= LOG2_MAX_SIZE - LOG2_MIN_SIZE; i++) {
			if (!LIST_EMPTY(&freeBlockLists[i])) { // found non empty BlockList
				block = LIST_LAST(&freeBlockLists[i]);
				pageInfo = to_page_info((uint32) block);
				pageInfo->num_of_free_blocks--;
				LIST_REMOVE(&freeBlockLists[i], block);
				return (void*) block;
			}
		}
		return NULL;
	}
}
//cprintf(" hi end");

//===========================
// [4] FREE BLOCK:
//===========================
void free_block(void *va) {

	{
		assert((uint32 )va >= dynAllocStart && (uint32 )va < dynAllocEnd);
	}

	uint32 block_size = get_block_size(va);
	struct PageInfoElement *page = to_page_info((uint32) va);

	int index = 0;
	uint32 alloc_size = -1;
	get_size_and_index(block_size, &alloc_size, &index);

	struct BlockElement *block = (struct BlockElement *) va;
	LIST_INSERT_HEAD(&freeBlockLists[index], block);

	page->num_of_free_blocks++;

	uint32 Number_Of_Blocks = PAGE_SIZE / block_size;
	if (page->num_of_free_blocks == Number_Of_Blocks) {
		char* current = (char*) to_page_va(page);

		for (uint32 i = 0; i < Number_Of_Blocks; i++) {
			struct BlockElement *b = (struct BlockElement *) current;
			LIST_REMOVE(&freeBlockLists[index], b);
			current += block_size;
		}

		page->block_size = 0;
		page->num_of_free_blocks = 0;

		return_page((void*) to_page_va(page));

		LIST_INSERT_TAIL(&freePagesList, page);
	}

}

//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//

//===========================
// [1] REALLOCATE BLOCK:
//===========================
void *realloc_block(void* va, uint32 new_size) {
//TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - realloc_block
//Your code is here
//Comment the following line
	panic("realloc_block() Not implemented yet");
}
