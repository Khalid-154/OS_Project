/*
 * fault_handler.c
 *
 *  Created on: Oct 12, 2022
 *      Author: HP
 */

#include "trap.h"
#include <kern/proc/user_environment.h>
#include <kern/cpu/sched.h>
#include <kern/cpu/cpu.h>
#include <kern/disk/pagefile_manager.h>
#include <kern/mem/memory_manager.h>
#include <kern/mem/kheap.h>

//2014 Test Free(): Set it to bypass the PAGE FAULT on an instruction with this length and continue executing the next one
// 0 means don't bypass the PAGE FAULT
uint8 bypassInstrLength = 0;
//===============================
// REPLACEMENT STRATEGIES
//===============================
//2020
void setPageReplacmentAlgorithmLRU(int LRU_TYPE)
{
	assert(LRU_TYPE == PG_REP_LRU_TIME_APPROX || LRU_TYPE == PG_REP_LRU_LISTS_APPROX);
	_PageRepAlgoType = LRU_TYPE ;
}
void setPageReplacmentAlgorithmCLOCK(){_PageRepAlgoType = PG_REP_CLOCK;}
void setPageReplacmentAlgorithmFIFO(){_PageRepAlgoType = PG_REP_FIFO;}
void setPageReplacmentAlgorithmModifiedCLOCK(){_PageRepAlgoType = PG_REP_MODIFIEDCLOCK;}
/*2018*/ void setPageReplacmentAlgorithmDynamicLocal(){_PageRepAlgoType = PG_REP_DYNAMIC_LOCAL;}
/*2021*/ void setPageReplacmentAlgorithmNchanceCLOCK(int PageWSMaxSweeps){_PageRepAlgoType = PG_REP_NchanceCLOCK;  page_WS_max_sweeps = PageWSMaxSweeps;}
/*2024*/ void setFASTNchanceCLOCK(bool fast){ FASTNchanceCLOCK = fast; };
/*2025*/ void setPageReplacmentAlgorithmOPTIMAL(){ _PageRepAlgoType = PG_REP_OPTIMAL; };

//2020
uint32 isPageReplacmentAlgorithmLRU(int LRU_TYPE){return _PageRepAlgoType == LRU_TYPE ? 1 : 0;}
uint32 isPageReplacmentAlgorithmCLOCK(){if(_PageRepAlgoType == PG_REP_CLOCK) return 1; return 0;}
uint32 isPageReplacmentAlgorithmFIFO(){if(_PageRepAlgoType == PG_REP_FIFO) return 1; return 0;}
uint32 isPageReplacmentAlgorithmModifiedCLOCK(){if(_PageRepAlgoType == PG_REP_MODIFIEDCLOCK) return 1; return 0;}
/*2018*/ uint32 isPageReplacmentAlgorithmDynamicLocal(){if(_PageRepAlgoType == PG_REP_DYNAMIC_LOCAL) return 1; return 0;}
/*2021*/ uint32 isPageReplacmentAlgorithmNchanceCLOCK(){if(_PageRepAlgoType == PG_REP_NchanceCLOCK) return 1; return 0;}
/*2021*/ uint32 isPageReplacmentAlgorithmOPTIMAL(){if(_PageRepAlgoType == PG_REP_OPTIMAL) return 1; return 0;}

//===============================
// PAGE BUFFERING
//===============================
void enableModifiedBuffer(uint32 enableIt){_EnableModifiedBuffer = enableIt;}
uint8 isModifiedBufferEnabled(){  return _EnableModifiedBuffer ; }

void enableBuffering(uint32 enableIt){_EnableBuffering = enableIt;}
uint8 isBufferingEnabled(){  return _EnableBuffering ; }

void setModifiedBufferLength(uint32 length) { _ModifiedBufferLength = length;}
uint32 getModifiedBufferLength() { return _ModifiedBufferLength;}

//===============================
// FAULT HANDLERS
//===============================

//==================
// [0] INIT HANDLER:
//==================
void fault_handler_init()
{
	//setPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX);
	//setPageReplacmentAlgorithmOPTIMAL();
	setPageReplacmentAlgorithmCLOCK();
	//setPageReplacmentAlgorithmModifiedCLOCK();
	enableBuffering(0);
	enableModifiedBuffer(0) ;
	setModifiedBufferLength(1000);
}
//==================
// [1] MAIN HANDLER:
//==================
/*2022*/
uint32 last_eip = 0;
uint32 before_last_eip = 0;
uint32 last_fault_va = 0;
uint32 before_last_fault_va = 0;
int8 num_repeated_fault  = 0;
extern uint32 sys_calculate_free_frames() ;

struct Env* last_faulted_env = NULL;
void fault_handler(struct Trapframe *tf)
{
	/******************************************************/
	// Read processor's CR2 register to find the faulting address
	uint32 fault_va = rcr2();
	//cprintf("************Faulted VA = %x************\n", fault_va);
	//	print_trapframe(tf);
	/******************************************************/

	//If same fault va for 3 times, then panic
	//UPDATE: 3 FAULTS MUST come from the same environment (or the kernel)
	struct Env* cur_env = get_cpu_proc();
	if (last_fault_va == fault_va && last_faulted_env == cur_env)
	{
		num_repeated_fault++ ;
		if (num_repeated_fault == 3)
		{
			print_trapframe(tf);
			panic("Failed to handle fault! fault @ at va = %x from eip = %x causes va (%x) to be faulted for 3 successive times\n", before_last_fault_va, before_last_eip, fault_va);
		}
	}
	else
	{
		before_last_fault_va = last_fault_va;
		before_last_eip = last_eip;
		num_repeated_fault = 0;
	}
	last_eip = (uint32)tf->tf_eip;
	last_fault_va = fault_va ;
	last_faulted_env = cur_env;
	/******************************************************/
	//2017: Check stack overflow for Kernel
	int userTrap = 0;
	if ((tf->tf_cs & 3) == 3) {
		userTrap = 1;
	}
	if (!userTrap)
	{
		struct cpu* c = mycpu();
		//cprintf("trap from KERNEL\n");
		if (cur_env && fault_va >= (uint32)cur_env->kstack && fault_va < (uint32)cur_env->kstack + PAGE_SIZE)
			panic("User Kernel Stack: overflow exception!");
		else if (fault_va >= (uint32)c->stack && fault_va < (uint32)c->stack + PAGE_SIZE)
			panic("Sched Kernel Stack of CPU #%d: overflow exception!", c - CPUS);
#if USE_KHEAP
		if (fault_va >= KERNEL_HEAP_MAX)
			panic("Kernel: heap overflow exception!");
#endif
	}
	//2017: Check stack underflow for User
	else
	{
		//cprintf("trap from USER\n");
		if (fault_va >= USTACKTOP && fault_va < USER_TOP)
			panic("User: stack underflow exception!");
	}

	//get a pointer to the environment that caused the fault at runtime
	//cprintf("curenv = %x\n", curenv);
	struct Env* faulted_env = cur_env;
	if (faulted_env == NULL)
	{
		cprintf("\nFaulted VA = %x\n", fault_va);
		print_trapframe(tf);
		panic("faulted env == NULL!");
	}
	//check the faulted address, is it a table or not ?
	//If the directory entry of the faulted address is NOT PRESENT then
	if ( (faulted_env->env_page_directory[PDX(fault_va)] & PERM_PRESENT) != PERM_PRESENT)
	{
		faulted_env->tableFaultsCounter ++ ;
		table_fault_handler(faulted_env, fault_va);
	}
	else
	{
		if (userTrap)
		{
		/*============================================================================================*/
		//TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #2 Check for invalid pointers
		//(e.g. pointing to unmarked user heap page, kernel or wrong access rights),
		//your code is here

			int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
			uint32 roundfault = ROUNDDOWN(fault_va,PAGE_SIZE);

//			if(roundfault >= USER_LIMIT){
			if(!(perms & PERM_USER) && (perms & PERM_PRESENT)){
				env_exit();
			}

			if(is_user_heap_page(roundfault)){
				if(!(perms & PERM_UHPAGE)){
					env_exit();
				}
			}

			if(!(perms & PERM_WRITEABLE) && (perms & PERM_PRESENT)){
				env_exit();
			}
			/*============================================================================================*/
		}

		/*2022: Check if fault due to Access Rights */
		int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
		if (perms & PERM_PRESENT)
			panic("Page @va=%x is exist! page fault due to violation of ACCESS RIGHTS\n", fault_va) ;
		/*============================================================================================*/


		// we have normal page fault =============================================================
		faulted_env->pageFaultsCounter ++ ;

//				cprintf("[%08s] user PAGE fault va %08x\n", faulted_env->prog_name, fault_va);
//				cprintf("\nPage working set BEFORE fault handler...\n");
//				env_page_ws_print(faulted_env);
		//int ffb = sys_calculate_free_frames();

		if(isBufferingEnabled())
		{
			__page_fault_handler_with_buffering(faulted_env, fault_va);
		}
		else
		{
			page_fault_handler(faulted_env, fault_va);
		}

		//		cprintf("\nPage working set AFTER fault handler...\n");
		//		env_page_ws_print(faulted_env);
		//		int ffa = sys_calculate_free_frames();
		//		cprintf("fault handling @%x: difference in free frames (after - before = %d)\n", fault_va, ffa - ffb);
	}

	/*************************************************************/
	//Refresh the TLB cache
	tlbflush();
	/*************************************************************/
}


//=========================
// [2] TABLE FAULT HANDLER:
//=========================
void table_fault_handler(struct Env * curenv, uint32 fault_va)
{
	//panic("table_fault_handler() is not implemented yet...!!");
	//Check if it's a stack page
	uint32* ptr_table;
#if USE_KHEAP
	{
		ptr_table = create_page_table(curenv->env_page_directory, (uint32)fault_va);
	}
#else
	{
		__static_cpt(curenv->env_page_directory, (uint32)fault_va, &ptr_table);
	}
#endif
}

//=========================
// [3] PAGE FAULT HANDLER:
//=========================
/* Calculate the number of page faults according th the OPTIMAL replacement strategy
 * Given:
 * 	1. Initial Working Set List (that the process started with)
 * 	2. Max Working Set Size
 * 	3. Page References List (contains the stream of referenced VAs till the process finished)
 *
 * 	IMPORTANT: This function SHOULD NOT change any of the given lists
 */
int get_optimal_num_faults(struct WS_List *initWorkingSet, int maxWSSize, struct PageRef_List *pageReferences)
{
	//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #2 get_optimal_num_faults
	//Your code is here
	//Comment the following line
	//panic("get_optimal_num_faults() is not implemented yet...!!");
    struct WS_List tempWS;
    LIST_INIT(&tempWS);
    struct WorkingSetElement *orig, *copy;
    LIST_FOREACH(orig, initWorkingSet)
    {
        copy = (struct WorkingSetElement*) kmalloc(sizeof(struct WorkingSetElement));
        copy->virtual_address = orig->virtual_address;
        LIST_INSERT_TAIL(&tempWS, copy);
    }

    int faults = 0;
    struct PageRefElement *ref;
    LIST_FOREACH(ref, pageReferences)
    {
        uint32 va = ref->virtual_address;
        int found = 0;
        struct WorkingSetElement *ws_e;
        LIST_FOREACH(ws_e, &tempWS)
        {
            if (ws_e->virtual_address == va)
            {
                found = 1;
                break;
            }
        }
        if (found)
            continue;
        faults++;
        if (LIST_SIZE(&tempWS) < maxWSSize)
        {
           struct WorkingSetElement *newItem = (struct WorkingSetElement*) kmalloc(sizeof(struct WorkingSetElement));
            newItem->virtual_address = va;
            LIST_INSERT_TAIL(&tempWS, newItem);
            continue;
        }
        int far = -1;
        struct WorkingSetElement *victim = NULL;
        LIST_FOREACH(ws_e, &tempWS)
        {
            int dis = 0;
            int found_again = 0;
            struct PageRefElement *future = ref->prev_next_info.le_next;
            while (future)
            {
                if (future->virtual_address == ws_e->virtual_address)
                {
                    found_again = 1;
                    break;
                }
                dis++;
                future = future->prev_next_info.le_next;
            }

            if (!found_again)
            {
                victim = ws_e;
                far = 1000000000;
                break;
            }
            else if (dis > far)
            {
                far = dis;
                victim = ws_e;
            }
        }
        LIST_REMOVE(&tempWS, victim);
        kfree(victim);
        struct WorkingSetElement *new_item = (struct WorkingSetElement*) kmalloc(sizeof(struct WorkingSetElement));
        new_item->virtual_address = va;
        LIST_INSERT_TAIL(&tempWS, new_item);
    }
    while (!LIST_EMPTY(&tempWS))
    {
        struct WorkingSetElement *ptr = LIST_FIRST(&tempWS);
        LIST_REMOVE(&tempWS, ptr);
        kfree(ptr);
    }

    return faults;
}



struct WS_List temp_ws;
int temp_WS_OPTIMAL_initialized = 0;

void page_fault_handler(struct Env * faulted_env, uint32 fault_va)
{
#if USE_KHEAP

	if (isPageReplacmentAlgorithmOPTIMAL())
		{
		//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #1 Optimal Replacement
		//Your code is here
		//Comment the following line
		//panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
	    static struct Env *temp_opt_env = NULL;
	    if (!temp_WS_OPTIMAL_initialized || temp_opt_env != faulted_env)
	    {
	        if (temp_WS_OPTIMAL_initialized)
	        {
	            while (!LIST_EMPTY(&temp_ws))
	            {
	                struct WorkingSetElement *t = LIST_FIRST(&temp_ws);
	                LIST_REMOVE(&temp_ws, t);
	                kfree(t);
	            }
	            temp_WS_OPTIMAL_initialized = 0;
	        }

	        LIST_INIT(&temp_ws);
	        struct WorkingSetElement *orignal, *copy;
	        LIST_FOREACH(orignal, &(faulted_env->page_WS_list))
	        {
	            copy = (struct WorkingSetElement*) kmalloc(sizeof(struct WorkingSetElement));
	            copy->virtual_address = orignal->virtual_address;
	            copy->empty = orignal->empty;
	            copy->time_stamp = orignal->time_stamp;
	            copy->sweeps_counter = orignal->sweeps_counter;
	            LIST_INSERT_TAIL(&temp_ws, copy);
	        }

	        temp_WS_OPTIMAL_initialized = 1;
	        temp_opt_env = faulted_env;
	    }
	    uint32 rva =  ROUNDDOWN(fault_va, PAGE_SIZE);
	    uint32 *pt = NULL;
	    struct FrameInfo *frame = get_frame_info(faulted_env->env_page_directory, rva, &pt);

	    if (frame == NULL)
	    {
	    	struct FrameInfo *new_frame = NULL;
			allocate_frame(&new_frame);
			map_frame(faulted_env->env_page_directory, new_frame, fault_va, PERM_USER | PERM_WRITEABLE | PERM_USED | PERM_PRESENT);
			int retopt = pf_read_env_page(faulted_env, (void*)fault_va);

			if(retopt == E_PAGE_NOT_EXIST_IN_PF){
				if((fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX) || (fault_va >= USTACKBOTTOM && fault_va < USTACKTOP)){
				}else{
					env_exit();
				}
			}

	        pt_set_page_permissions(faulted_env->env_page_directory, rva, PERM_PRESENT, 0);
	    }
	    else
	    {
	        pt_set_page_permissions(faulted_env->env_page_directory, rva, PERM_PRESENT, 0);
	    }

	    int exist = 0;
	    struct WorkingSetElement *copy;
	    LIST_FOREACH(copy, &temp_ws)
	    {
	        if (copy->virtual_address == rva)
	        {
	        	exist = 1;
	            break;
	        }
	    }

	    int temp_size = LIST_SIZE(&temp_ws);
	    int ws_max = faulted_env->page_WS_max_size;
	    if (!exist && temp_size == ws_max)
	    {
	        while (!LIST_EMPTY(&temp_ws))
	        {
	            struct WorkingSetElement *t = LIST_FIRST(&temp_ws);
	            pt_set_page_permissions(faulted_env->env_page_directory, t->virtual_address, 0, PERM_PRESENT);
	            LIST_REMOVE(&temp_ws, t);
	            kfree(t);
	        }
	    }

	    if (!exist)
	    {
	        struct WorkingSetElement *new_copy = (struct WorkingSetElement*) kmalloc(sizeof(struct WorkingSetElement));
	        new_copy->virtual_address = rva;
	        new_copy->empty = 0;
	        new_copy->time_stamp = 0;
	        new_copy->sweeps_counter = 0;
	        LIST_INSERT_TAIL(&temp_ws, new_copy);
	    }

	    struct PageRefElement *pre = (struct PageRefElement*) kmalloc(sizeof(struct PageRefElement));
	    pre->virtual_address = rva;
	    LIST_INSERT_TAIL(&(faulted_env->referenceStreamList), pre);
		}
	else
	{
		struct WorkingSetElement *victimWSElement = NULL;
		uint32 wsSize = LIST_SIZE(&(faulted_env->page_WS_list));
		if(wsSize < (faulted_env->page_WS_max_size))
		{
		//TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #3 placement
		//Your code is here
		//Comment the following line
		//panic("page_fault_handler().PLACEMENT is not implemented yet...!!");
		struct FrameInfo *ptr_on_frame_info = NULL;
		allocate_frame(&ptr_on_frame_info);
		map_frame(faulted_env->env_page_directory, ptr_on_frame_info,fault_va, PERM_USER | PERM_WRITEABLE);
		int retplac = pf_read_env_page(faulted_env, (void*)fault_va);
		 if (retplac == E_PAGE_NOT_EXIST_IN_PF)
		 {
			if ((fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX) ||(fault_va >= USTACKBOTTOM && fault_va < USTACKTOP))
			{
			} else {
			  env_exit();
			}
		  }
		 struct WorkingSetElement *ptr_last=env_page_ws_list_create_element(faulted_env,fault_va);
		 if(!ptr_last)
			 panic("cannot create ws element");
		 if(faulted_env->prp==0)
		 {
			 LIST_INSERT_TAIL(&faulted_env->page_WS_list,ptr_last);
			 if(LIST_SIZE(&faulted_env->page_WS_list)==faulted_env->page_WS_max_size)
			 {
				 faulted_env->prp=1;
				 faulted_env->page_last_WS_element=LIST_FIRST(&faulted_env->page_WS_list);
			 }else {
//				 faulted_env->page_last_WS_element = NULL;
			 }
		 }else {
			 struct WorkingSetElement *clock_ptr=faulted_env->page_last_WS_element;
			 LIST_INSERT_BEFORE(&faulted_env->page_WS_list,clock_ptr,ptr_last);
		  }
		}
		else
		{
			if (isPageReplacmentAlgorithmCLOCK())
			{
				//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #3 Clock Replacement
				//Your code is here
				//Comment the following line
				//panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
				struct WS_List *wl = &faulted_env->page_WS_list;
				struct WorkingSetElement *wse = faulted_env->page_last_WS_element;
				struct WorkingSetElement *victim = NULL;
				while (1){
					if (!wse)
						wse = LIST_FIRST(wl);

					if(!wse)
						panic("WS is empty");

					uint32 perms = pt_get_page_permissions(faulted_env->env_page_directory, wse->virtual_address);

					if (!(perms & PERM_PRESENT)) {
						wse = LIST_NEXT(wse);
						continue;
					}

					if (perms & PERM_USED)
					{
						pt_set_page_permissions(faulted_env->env_page_directory, wse->virtual_address, 0, PERM_USED);
						wse = LIST_NEXT(wse);
					}
					else
					{
						victim = wse;
						break;
					}
				}

				uint32 victim_va = victim->virtual_address;
				uint32 perms = pt_get_page_permissions(faulted_env->env_page_directory, victim_va);
				uint32 *temp_table = NULL;
				struct FrameInfo *victim_frame = get_frame_info(faulted_env->env_page_directory, victim_va, &temp_table);
				//write on disk if modified
				uint32 victim_perms = pt_get_page_permissions(faulted_env->env_page_directory, victim_va);
				if (victim_perms & PERM_MODIFIED)
				{
					pf_update_env_page(faulted_env, victim_va, victim_frame);
				}

			    unmap_frame(faulted_env->env_page_directory, victim_va);
				//placement CLOCK
				struct FrameInfo *new_frame = NULL;
				allocate_frame(&new_frame);
				map_frame(faulted_env->env_page_directory, new_frame, fault_va, PERM_USER | PERM_WRITEABLE | PERM_USED);

				int retclk = pf_read_env_page(faulted_env, (void*)fault_va);
				if(retclk == E_PAGE_NOT_EXIST_IN_PF){
					if((fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX) || (fault_va >= USTACKBOTTOM && fault_va < USTACKTOP)){
					}else{
						env_exit();
					}
				}

				struct WorkingSetElement *victim_prev = LIST_PREV(victim);
				LIST_REMOVE(&faulted_env->page_WS_list, victim);
				struct WorkingSetElement *n_element = env_page_ws_list_create_element(faulted_env, fault_va);
				if(!n_element)
					panic("cannot create WS element for new page");
				if(!victim_prev)
					LIST_INSERT_HEAD(&faulted_env->page_WS_list, n_element);
				else
					LIST_INSERT_AFTER(&faulted_env->page_WS_list, victim_prev, n_element);

				faulted_env->page_last_WS_element = LIST_NEXT(n_element);
				if(!faulted_env->page_last_WS_element)
					faulted_env->page_last_WS_element = LIST_FIRST(&faulted_env->page_WS_list);

				pt_set_page_permissions(faulted_env->env_page_directory, fault_va, PERM_USED, 0);
				faulted_env->prp = 1;

			}
			else if (isPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX))
			{
				//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #2 LRU Aging Replacement
				//Your code is here
				//Comment the following line
				//panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
				struct WorkingSetElement *wse = NULL;
				struct WorkingSetElement *victim = NULL;
				uint32 min_age = 0xFFFFFFFF;

				LIST_FOREACH(wse, &(faulted_env->page_WS_list))
				{
					if(wse->time_stamp < min_age){
						min_age = wse->time_stamp;
						victim = wse;
					}
				}

				uint32 victim_va = victim->virtual_address;
				uint32 *ptr_page_table = NULL;
				struct FrameInfo *victim_frame = get_frame_info(faulted_env->env_page_directory, victim_va, &ptr_page_table);
				uint32 victim_perms = pt_get_page_permissions(faulted_env->env_page_directory, victim_va);

				if(ptr_page_table != NULL && ((victim_perms & PERM_MODIFIED) != 0)){
					pf_update_env_page(faulted_env, victim_va, victim_frame);
				}
				env_page_ws_invalidate(faulted_env, victim_va);
				//placement LRU
				struct FrameInfo *new_frame = NULL;
				allocate_frame(&new_frame);
				map_frame(faulted_env->env_page_directory, new_frame, fault_va, PERM_USER | PERM_WRITEABLE | PERM_USED | PERM_PRESENT);
				int retlru = pf_read_env_page(faulted_env, (void*)fault_va);

				if(retlru == E_PAGE_NOT_EXIST_IN_PF){
					if((fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX) || (fault_va >= USTACKBOTTOM && fault_va < USTACKTOP)){
					}else{
						env_exit();
					}
				}

				struct WorkingSetElement *new_wkst_elem = env_page_ws_list_create_element(faulted_env, fault_va);
				if(new_wkst_elem != NULL){
					new_wkst_elem->time_stamp = 0;
					LIST_INSERT_TAIL(&(faulted_env->page_WS_list), new_wkst_elem);
				}

				if(LIST_SIZE(&(faulted_env->page_WS_list)) == faulted_env->page_WS_max_size){
					faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
				}

			}
			else if (isPageReplacmentAlgorithmModifiedCLOCK())
			{
				//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #3 Modified Clock Replacement
				//Your code is here
				//Comment the following line
				//panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
				struct WorkingSetElement *wse = faulted_env->page_last_WS_element;
				struct WorkingSetElement *victim = NULL;

				try_again:
				for (int i = 0; i < faulted_env->page_WS_max_size; i++)
				{
				    uint32 va = wse->virtual_address;
				    uint32 perms = pt_get_page_permissions(faulted_env->env_page_directory, va);
				    	int used = (perms & PERM_USED);
				    	int modified = (perms & PERM_MODIFIED);
				    	if ((used == 0) && (modified == 0))
				    	{
				    		victim = wse;
				    		wse = LIST_NEXT(wse);
							if(wse == NULL){
								wse = LIST_FIRST(&faulted_env->page_WS_list);}
					    	faulted_env->page_last_WS_element= wse;
				    		break;
				    	}else{
							wse = LIST_NEXT(wse);
				    		if(wse == NULL){
				    			wse = LIST_FIRST(&faulted_env->page_WS_list);
				    		}
				    	}
				}
					if(victim == NULL){
					wse = faulted_env->page_last_WS_element;
					for (int i = 0; i < faulted_env->page_WS_max_size; i++){
					//TRY 2 : USED = 0 (clear all used = 1 on the way)
				    uint32 va = wse->virtual_address;
				    uint32 perms = pt_get_page_permissions(faulted_env->env_page_directory, va);
				    int used = (perms & PERM_USED);
				    if (used!=0){
				    pt_set_page_permissions(faulted_env->env_page_directory, va, 0, PERM_USED);
					wse = LIST_NEXT(wse);
					if(wse == NULL){
					wse = LIST_FIRST(&faulted_env->page_WS_list);
					}
				    }else {
				    	victim = wse;
				    	faulted_env->page_last_WS_element= wse;
				    	uint32 victim_va = victim->virtual_address;
				    	uint32 *pt = NULL;
				    	struct FrameInfo *victim_frame = get_frame_info(faulted_env->env_page_directory, victim_va, &pt);
				    	pt_set_page_permissions(faulted_env->env_page_directory, victim_va, 0, PERM_MODIFIED);
				    	pf_update_env_page(faulted_env, victim_va, victim_frame);
				    	wse = LIST_NEXT(wse);
						if(wse == NULL){
							wse = LIST_FIRST(&faulted_env->page_WS_list);}
				    	break;
				    }
				  }
				}
					if(victim == NULL){
						goto try_again;
						}

					env_page_ws_invalidate(faulted_env, victim->virtual_address);
					//placement MODCLOCK
					struct FrameInfo *new_frame = NULL;
					allocate_frame(&new_frame);
					map_frame(faulted_env->env_page_directory, new_frame, fault_va, PERM_USER | PERM_WRITEABLE | PERM_USED | PERM_PRESENT);
					int retmod = pf_read_env_page(faulted_env, (void*)fault_va);
					if(retmod == E_PAGE_NOT_EXIST_IN_PF){
						if((fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX) || (fault_va >= USTACKBOTTOM && fault_va < USTACKTOP)){
						}else{
							env_exit();
						}
					}
					struct WorkingSetElement *new_wkst_elem = env_page_ws_list_create_element(faulted_env, fault_va);
					if(faulted_env->page_last_WS_element != NULL){
						LIST_INSERT_BEFORE(&(faulted_env->page_WS_list), faulted_env->page_last_WS_element, new_wkst_elem);
					}
					else{
						LIST_INSERT_TAIL(&(faulted_env->page_WS_list), new_wkst_elem);

						faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
					}
					faulted_env->prp=1;
			}
		}
	}
#endif
}

void __page_fault_handler_with_buffering(struct Env * curenv, uint32 fault_va)
{
	panic("this function is not required...!!");
}


