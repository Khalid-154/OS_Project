// Kernel-level Semaphore

#include "inc/types.h"
#include "inc/x86.h"
#include "inc/memlayout.h"
#include "inc/mmu.h"
#include "inc/environment_definitions.h"
#include "inc/assert.h"
#include "inc/string.h"
#include "ksemaphore.h"
#include "channel.h"
#include "../cpu/cpu.h"
#include "../proc/user_environment.h"

void init_ksemaphore(struct ksemaphore *ksem, int value, char *name) {
	init_channel(&(ksem->chan), "ksemaphore channel");
	init_kspinlock(&(ksem->lk), "lock of ksemaphore");
	strcpy(ksem->name, name);
	ksem->count = value;
}

void wait_ksemaphore(struct ksemaphore *Ksemaphore) {
	acquire_kspinlock(&(Ksemaphore->lk));
	Ksemaphore->count--;
	if (Ksemaphore->count < 0)
		sleep(&(Ksemaphore->chan), &(Ksemaphore->lk));
	release_kspinlock(&(Ksemaphore->lk));
}

void signal_ksemaphore(struct ksemaphore *Ksemaphore) {
	acquire_kspinlock(&(Ksemaphore->lk));
	Ksemaphore->count++;
	if (Ksemaphore->count <= 0)
		wakeup_one(&(Ksemaphore->chan));
	release_kspinlock(&(Ksemaphore->lk));
}
