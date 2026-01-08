// Sleeping locks

#include "inc/types.h"
#include "inc/x86.h"
#include "inc/memlayout.h"
#include "inc/mmu.h"
#include "inc/environment_definitions.h"
#include "inc/assert.h"
#include "inc/string.h"
#include "sleeplock.h"
#include "channel.h"
#include "../cpu/cpu.h"
#include "../proc/user_environment.h"

void init_sleeplock(struct sleeplock *lk, char *name) {
	init_channel(&(lk->chan), "sleep lock channel");
	char prefix[30] = "lock of sleeplock - ";
	char guardName[30 + NAMELEN];
	strcconcat(prefix, name, guardName);
	init_kspinlock(&(lk->lk), guardName);
	strcpy(lk->name, name);
	lk->locked = 0;
	lk->pid = 0;
}

void acquire_sleeplock(struct sleeplock *OurLock) {
	acquire_kspinlock(&(OurLock->lk));
	while (OurLock->locked)
		sleep(&(OurLock->chan), &(OurLock->lk));
	OurLock->locked = 1;
	OurLock->pid = get_cpu_proc()->env_id;
	release_kspinlock(&(OurLock->lk));
}

void release_sleeplock(struct sleeplock *OurLock) {
	assert(holding_sleeplock(OurLock));
	acquire_kspinlock(&(OurLock->lk));
	OurLock->locked = 0, OurLock->pid = 0;
	if (queue_size(&(OurLock->chan.queue)) > 0)
		wakeup_all(&(OurLock->chan));
	release_kspinlock(&(OurLock->lk));
}

int holding_sleeplock(struct sleeplock *lk) {
	int r;
	acquire_kspinlock(&(lk->lk));
	r = lk->locked && (lk->pid == get_cpu_proc()->env_id);
	release_kspinlock(&(lk->lk));
	return r;
}
