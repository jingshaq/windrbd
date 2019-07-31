﻿#ifndef __WAIT_H__
#define __WAIT_H__
#include <wdm.h>
#include "linux/list.h"
#include "linux/spinlock.h"

/* TODO: location of this should probably be linux/wait.h */

	/* TODO: this is unused: */
typedef struct _wait_queue
{
	unsigned int flags;
	struct list_head task_list;
} wait_queue_t;

typedef struct wait_queue_head
{
	spinlock_t lock;
	struct list_head task_list;
	KEVENT	wqh_event;
#define Q_NAME_SZ	16 
	char eventName[Q_NAME_SZ];
} wait_queue_head_t;

#define DEFINE_WAIT(w) void *(w)

void prepare_to_wait(struct wait_queue_head *w, void *unused, int interruptible);
void finish_wait(struct wait_queue_head *w, void *unused);

/* This will be the schedule() function soon ... */
void new_schedule(void);
/* Returns -EINTR on signal else remaining time. */
ULONG_PTR new_schedule_timeout(ULONG_PTR timeout);
/* Returns -EINTR on signal else remaining time. Use only internally. */
/* TODO: better name for functiokn */
LONG_PTR new_schedule_timeout_maybe_interrupted(ULONG_PTR timeout);

/* One macro for all cases of wait_event: if there is a bug it is
 * most likely in here ...
 */

#define ll_wait_event_macro(ret, wait_queue, condition, timeout, interruptible) \
do {									\
	LONG_PTR __timeout = timeout;					\
	while (1) {							\
		prepare_to_wait(&wait_queue, NULL, interruptible);	\
		if (condition)						\
			break;						\
									\
		if (__timeout == 0)					\
			break;						\
									\
		__timeout = new_schedule_timeout_maybe_interrupted(__timeout);\
		if (interruptible == TASK_INTERRUPTIBLE &&		\
		   __timeout == -EINTR)					\
			break;						\
	}								\
	finish_wait(&wait_queue, NULL);					\
	ret = __timeout;						\
} while (0);

/* TODO: those two should honor current->state */
#define wait_event(wait_queue, condition)				\
do {									\
	int unused;							\
	ll_wait_event_macro(unused, wait_queue, condition,		\
		MAX_SCHEDULE_TIMEOUT, TASK_INTERRUPTIBLE);		\
} while (0);

	/* TODO: this might 'return' -EINTR */
#define wait_event_timeout(ret, wait_queue, condition, timeout)		\
do {									\
	ll_wait_event_macro(ret, wait_queue, condition,			\
		timeout, TASK_INTERRUPTIBLE);				\
} while (0);

#define wait_event_interruptible(ret, wait_queue, condition)		\
do {									\
	ll_wait_event_macro(ret, wait_queue, condition,			\
		MAX_SCHEDULE_TIMEOUT, TASK_INTERRUPTIBLE);		\
} while (0);

#define wait_event_interruptible_timeout(ret, wait_queue, condition, timeout) \
do {									\
	ll_wait_event_macro(ret, wait_queue, condition,			\
		timeout, TASK_INTERRUPTIBLE);		\
} while (0);


#endif
