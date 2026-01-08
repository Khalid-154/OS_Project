/*
 * channel.c
 *
 *  Created on: Sep 22, 2024
 *      Author: HP
 */
#include "channel.h"
#include <kern/proc/user_environment.h>
#include <kern/cpu/sched.h>
#include <inc/string.h>
#include <inc/disk.h>

//===============================
// 1) INITIALIZE THE CHANNEL:
//===============================
// initialize its lock & queue
void init_channel(struct Channel *chan, char *name) {
	strcpy(chan->name, name);
	init_queue(&(chan->queue));
}

//===============================
// 2) SLEEP ON A GIVEN CHANNEL:
//===============================
// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
// Ref: xv6-x86 OS code
void sleep(struct Channel *chan, struct kspinlock *OurLock) {
	acquire_kspinlock(&ProcessQueues.qlock);
	if (OurLock != NULL)
		release_kspinlock(OurLock);
	struct Env *cur = get_cpu_proc();
	cur->env_status = ENV_BLOCKED;
	enqueue(&(chan->queue), cur);
	sched();
	if (OurLock != NULL)
		acquire_kspinlock(OurLock);
	release_kspinlock(&ProcessQueues.qlock);
}

//==================================================
// 3) WAKEUP ONE BLOCKED PROCESS ON A GIVEN CHANNEL:
//==================================================
// Wake up ONE process sleeping on chan.
// The qlock must be held.
// Ref: xv6-x86 OS code
// chan MUST be of type "struct Env_Queue" to hold the blocked processes
void wakeup_one(struct Channel *chan) {
	acquire_kspinlock(&ProcessQueues.qlock);
	struct Env *cur = dequeue(&(chan->queue));
	if (cur != NULL) {
		cur->env_status = ENV_READY;
		sched_insert_ready(cur);
	}
	release_kspinlock(&ProcessQueues.qlock);
}

//====================================================
// 4) WAKEUP ALL BLOCKED PROCESSES ON A GIVEN CHANNEL:
//====================================================
// Wake up all processes sleeping on chan.
// The queues lock must be held.
// Ref: xv6-x86 OS code
// chan MUST be of type "struct Env_Queue" to hold the blocked processes

void wakeup_all(struct Channel *chan) {
	acquire_kspinlock(&ProcessQueues.qlock);
	struct Env *all;
	while ((all = dequeue(&(chan->queue))) != NULL) {
		all->env_status = ENV_READY;
		sched_insert_ready(all);
	}
	release_kspinlock(&ProcessQueues.qlock);
}

