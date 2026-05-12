/* 이 파일은 교육용 운영체제 Nachos의 소스 코드에서 파생되었습니다.
   Nachos 저작권 고지는 아래에 전체 내용이 재현되어 있습니다. */

/* 저작권 (c) 1992-1996 The Regents of the University of California.
   모든 권리를 보유합니다.

   이 소프트웨어와 문서를 어떠한 목적으로든 사용, 복사, 수정, 배포할 수 있는
   권한은 비용 없이, 별도의 서면 동의 없이 허가됩니다. 단, 위 저작권 고지와
   아래 두 문단이 이 소프트웨어의 모든 복사본에 포함되어야 합니다.

   어떠한 경우에도 캘리포니아 대학교는 이 소프트웨어와 문서의 사용으로 인해
   발생하는 직접, 간접, 특별, 우발적 또는 결과적 손해에 대해 어느 당사자에게도
   책임을 지지 않습니다. 이는 캘리포니아 대학교가 그러한 손해의 가능성을
   사전에 고지받은 경우에도 마찬가지입니다.

   캘리포니아 대학교는 상품성 및 특정 목적 적합성에 대한 묵시적 보증을
   포함하되 이에 한정되지 않는 모든 보증을 명시적으로 부인합니다.
   아래에 제공되는 소프트웨어는 "있는 그대로" 제공됩니다.
  
   또한 캘리포니아 대학교는 유지보수, 지원, 업데이트, 개선 또는 수정을
   제공할 의무가 없습니다.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

void recalculate_priority (struct lock *lock);

static bool priority_more_func (const struct list_elem* a ,const struct list_elem* b,void* aux) {
	struct thread* thread_a = list_entry(a,struct thread,elem);
	struct thread* thread_b = list_entry(b,struct thread,elem);

	// printf("================ a:%d b:%d\n", thread_a->priority, thread_b->priority);

	return thread_a->priority>thread_b->priority;
}

static bool donate_priority_more_func (const struct list_elem* a ,const struct list_elem* b,void* aux) {
	struct thread* thread_a = list_entry(a,struct thread, donator_elem);
	struct thread* thread_b = list_entry(b,struct thread, donator_elem);

	// printf("================ a:%d b:%d\n", thread_a->priority, thread_b->priority);

	return thread_a->priority>thread_b->priority;
}

/* 세마포어 SEMA를 VALUE로 초기화합니다. 세마포어는 음수가 아닌 정수와,
   이를 조작하는 두 개의 원자적 연산으로 이루어집니다.

   - down 또는 "P": 값이 양수가 될 때까지 기다린 뒤 값을 감소시킵니다.

   - up 또는 "V": 값을 증가시킵니다. 기다리는 스레드가 있다면 하나를
   깨웁니다. */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* 세마포어에 대한 down 또는 "P" 연산입니다. SEMA의 값이 양수가 될 때까지
   기다린 뒤 원자적으로 값을 감소시킵니다.

   이 함수는 잠들 수 있으므로 인터럽트 핸들러 안에서 호출하면 안 됩니다.
   인터럽트가 비활성화된 상태에서 호출할 수는 있지만, 잠들게 되면 다음에
   스케줄되는 스레드가 인터럽트를 다시 켤 가능성이 큽니다. 이것은
   sema_down 함수입니다. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		// list_push_back (&sema->waiters, &thread_current ()->elem);
		list_insert_ordered (&(sema->waiters), &thread_current()->elem, priority_more_func, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* 세마포어에 대한 down 또는 "P" 연산입니다. 단, 세마포어가 이미 0이 아닐
   때만 수행합니다. 세마포어 값이 감소되면 true를, 그렇지 않으면 false를
   반환합니다.

   이 함수는 인터럽트 핸들러에서 호출할 수 있습니다. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* 세마포어에 대한 up 또는 "V" 연산입니다. SEMA의 값을 증가시키고,
   SEMA를 기다리는 스레드가 있다면 그중 하나를 깨웁니다.

   이 함수는 인터럽트 핸들러에서 호출할 수 있습니다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	struct thread* t;
	t = NULL;
	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)){
		list_sort (&(sema->waiters), priority_more_func, NULL);
		t=list_entry (list_pop_front (&sema->waiters), struct thread, elem);
		thread_unblock (t);
	}
	sema->value++;
	intr_set_level (old_level);
	if (t != NULL && t->priority > thread_current()->priority) {
		if (intr_context ())
			intr_yield_on_return ();
		else
			thread_yield ();
	}
}

static void sema_test_helper (void *sema_);

/* 두 스레드 사이에서 제어 흐름이 "핑퐁"처럼 오가게 만드는 세마포어
   자체 테스트입니다. 무슨 일이 일어나는지 보려면 printf() 호출을 넣으세요. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* sema_self_test()에서 사용하는 스레드 함수입니다. */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* LOCK을 초기화합니다. 락은 어느 시점이든 최대 하나의 스레드만 보유할 수
   있습니다. 이 락은 "재귀적"이지 않습니다. 즉, 현재 락을 보유한 스레드가
   같은 락을 다시 획득하려고 하면 오류입니다.

   락은 초기값이 1인 세마포어를 특수화한 것입니다. 락과 그런 세마포어의
   차이는 두 가지입니다. 첫째, 세마포어는 1보다 큰 값을 가질 수 있지만,
   락은 한 번에 하나의 스레드만 소유할 수 있습니다. 둘째, 세마포어에는
   소유자가 없습니다. 즉, 한 스레드가 세마포어에 "down"을 수행하고 다른
   스레드가 "up"을 수행할 수 있습니다. 하지만 락은 같은 스레드가 획득과
   해제를 모두 수행해야 합니다. 이런 제약이 부담스럽다면 락 대신 세마포어를
   사용해야 한다는 좋은 신호입니다. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

void
recalculate_priority (struct lock *lock) {
	struct thread* t;
	// lock이 NULL이거나 lock->holder가 NULL이면
    // 	return;
	t = lock->holder;
	
	int max_donated_priority = 0;
    if (!list_empty(&t->donators)) {
		list_sort(&t->donators, donate_priority_more_func, NULL);
		max_donated_priority = list_entry(list_front(&t->donators), struct thread, donator_elem)->priority;
	}

    int old_p = t->priority;
	t->priority = t->base_priority < max_donated_priority ? max_donated_priority : t->base_priority;
    if ((old_p != t->priority) && (t->waiting_lock != NULL)) {
		recalculate_priority (t->waiting_lock);
	}        
}

void
remove_donate (struct lock *lock) { // lock->holder의 (donator_list를 비우고) priority를 재계산
	struct thread* t = lock->holder;
	struct list_elem* donator_elem = list_begin (&t->donators);
    while(!list_empty (&t->donators)) {
		if (donator_elem == list_end(&t->donators)) break;
		struct thread* donator = list_entry (donator_elem, struct thread, donator_elem);
        if (donator->waiting_lock == lock) {
			donator->waiting_lock = NULL;
			list_remove (&donator->donator_elem);
		}
		donator_elem = donator_elem->next;
	}
	recalculate_priority (lock);
}  

void 
donate_to_lock_holder (struct lock *lock) {
    struct thread* cur = thread_current ();
    cur->waiting_lock = lock;

    list_insert_ordered (&lock->holder->donators, &cur->donator_elem, donate_priority_more_func, NULL);

	/* 디버그: 락 보유자와 모든 기부자를 출력합니다. */

    // printf ("\n[우선순위 기부 디버그]\n");

    // printf ("보유자: 이름=%s tid=%d 우선순위=%d 기본_우선순위=%d\n",
    //         lock->holder->name,
    //         lock->holder->tid,
    //         lock->holder->priority,
    //         lock->holder->base_priority);
    // printf ("기부자:\n");
    // struct list_elem *e;
    // int idx = 0;

    // for (e = list_begin (&lock->holder->donators);
    //      e != list_end (&lock->holder->donators);
    //      e = list_next (e)) {
    //     struct thread *donator = list_entry (e, struct thread, donator_elem);

    //     printf ("  [%d] 이름=%s tid=%d 우선순위=%d 기본_우선순위=%d 대기_락=%p\n",
    //             idx,
    //             donator->name,
    //             donator->tid,
    //             donator->priority,
    //             donator->base_priority,
    //             donator->waiting_lock);
    //     idx++;
    // }

    // printf ("[/우선순위 기부 디버그]\n\n");
    recalculate_priority (lock);
}

/* LOCK을 획득합니다. 필요하다면 LOCK을 사용할 수 있을 때까지 잠듭니다.
   현재 스레드가 이미 이 락을 보유하고 있으면 안 됩니다.

   이 함수는 잠들 수 있으므로 인터럽트 핸들러 안에서 호출하면 안 됩니다.
   인터럽트가 비활성화된 상태에서 호출할 수는 있지만, 잠들어야 한다면
   인터럽트는 다시 켜집니다. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	if (lock->holder != NULL) {
		donate_to_lock_holder (lock);
	}
	sema_down (&lock->semaphore);
	lock->holder = thread_current ();
}

/* LOCK 획득을 시도하고, 성공하면 true를 실패하면 false를 반환합니다.
   현재 스레드가 이미 이 락을 보유하고 있으면 안 됩니다.

   이 함수는 잠들지 않으므로 인터럽트 핸들러 안에서 호출할 수 있습니다. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* LOCK을 해제합니다. LOCK은 현재 스레드가 소유하고 있어야 합니다.
   이것은 lock_release 함수입니다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 인터럽트 핸들러 안에서 락을
   해제하려고 시도하는 것은 의미가 없습니다. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	remove_donate (lock);
	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* 현재 스레드가 LOCK을 보유하고 있으면 true를, 그렇지 않으면 false를
   반환합니다. 다른 스레드가 락을 보유하는지 검사하는 것은 경쟁 조건을
   일으킬 수 있음에 주의하세요. */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* 리스트 안의 세마포어 하나입니다. */
struct semaphore_elem {
	struct list_elem elem;              /* 리스트 원소입니다. */
	struct semaphore semaphore;         /* 이 세마포어입니다. */
};

static bool priority_more_semaphore_elem_func (const struct list_elem* semaphore_elem_elem_a,const struct list_elem* semaphore_elem_elem_b,void* aux) {
	struct semaphore_elem* semaphore_elem_a = list_entry(semaphore_elem_elem_a, struct semaphore_elem, elem);
	struct semaphore_elem* semaphore_elem_b = list_entry(semaphore_elem_elem_b, struct semaphore_elem, elem);

	struct list_elem* thread_elem_a = list_front(&semaphore_elem_a->semaphore.waiters);
	struct list_elem* thread_elem_b = list_front(&semaphore_elem_b->semaphore.waiters);

	struct thread* thread_a = list_entry(thread_elem_a, struct thread, elem);
	struct thread* thread_b = list_entry(thread_elem_b, struct thread, elem);

	return thread_a->priority > thread_b->priority;
}

/* 조건 변수 COND를 초기화합니다. 조건 변수는 한 코드 조각이 조건을 알리고,
   협력하는 코드가 그 신호를 받아 그에 따라 동작할 수 있게 합니다. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* LOCK을 원자적으로 해제하고, 다른 코드 조각이 COND에 신호를 보낼 때까지
   기다립니다. COND에 신호가 오면 반환하기 전에 LOCK을 다시 획득합니다.
   이 함수를 호출하기 전에 LOCK을 보유하고 있어야 합니다.

   이 함수가 구현하는 모니터는 "Hoare" 방식이 아니라 "Mesa" 방식입니다.
   즉, 신호를 보내고 받는 일이 원자적 연산이 아닙니다. 따라서 일반적으로
   호출자는 대기가 끝난 뒤 조건을 다시 확인하고, 필요하다면 다시 기다려야
   합니다.

   주어진 조건 변수는 하나의 락에만 연결됩니다. 하지만 하나의 락은 여러 조건
   변수와 연결될 수 있습니다. 즉, 락에서 조건 변수로는 일대다 매핑이 있습니다.

   이 함수는 잠들 수 있으므로 인터럽트 핸들러 안에서 호출하면 안 됩니다.
   인터럽트가 비활성화된 상태에서 호출할 수는 있지만, 잠들어야 한다면
   인터럽트는 다시 켜집니다. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);
	// list_insert_ordered (&cond->waiters, &waiter.elem, priority_more_semaphore_elem_func, NULL); // list_insert_ordered(&리스트, &넣을_구조체->elem, 비교함수, NULL);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* LOCK으로 보호되는 COND를 기다리는 스레드가 있다면, 이 함수는 그중 하나에
   신호를 보내 대기 상태에서 깨웁니다. 이 함수를 호출하기 전에 LOCK을
   보유하고 있어야 합니다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 인터럽트 핸들러 안에서 조건
   변수에 신호를 보내려고 시도하는 것은 의미가 없습니다. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)){
		list_sort (&(cond->waiters), priority_more_semaphore_elem_func, NULL); // list_sort(&정렬할_리스트, 비교함수, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
}

/* LOCK으로 보호되는 COND를 기다리는 모든 스레드를 깨웁니다. 기다리는
   스레드가 없으면 아무 일도 하지 않습니다. 이 함수를 호출하기 전에 LOCK을
   보유하고 있어야 합니다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 인터럽트 핸들러 안에서 조건
   변수에 신호를 보내려고 시도하는 것은 의미가 없습니다. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
