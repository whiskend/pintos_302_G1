#ifndef THREADS_VADDR_H
#define THREADS_VADDR_H

#include <debug.h>
#include <stdint.h>
#include <stdbool.h>

#include "threads/loader.h"

/* 가상 주소를 다루는 함수와 매크로입니다.
 *
 * x86 하드웨어 페이지 테이블에 특화된 함수와 매크로는 pte.h를 참고하세요. */

#define BITMASK(SHIFT, CNT) (((1ul << (CNT)) - 1) << (SHIFT))

/* 페이지 오프셋입니다. (0:12 비트) */
#define PGSHIFT 0                          /* 첫 오프셋 비트의 인덱스입니다. */
#define PGBITS  12                         /* 오프셋 비트 수입니다. */
#define PGSIZE  (1 << PGBITS)              /* 한 페이지의 바이트 수입니다. */
#define PGMASK  BITMASK(PGSHIFT, PGBITS)   /* 페이지 오프셋 비트입니다. (0:12) */

/* 페이지 안에서의 오프셋입니다. */
#define pg_ofs(va) ((uint64_t) (va) & PGMASK)

#define pg_no(va) ((uint64_t) (va) >> PGBITS)

/* 가장 가까운 위쪽 페이지 경계로 올림합니다. */
#define pg_round_up(va) ((void *) (((uint64_t) (va) + PGSIZE - 1) & ~PGMASK))

/* 가장 가까운 아래쪽 페이지 경계로 내림합니다. */
#define pg_round_down(va) (void *) ((uint64_t) (va) & ~PGMASK)

/* 커널 가상 주소의 시작점입니다: 0x8004000000 */
#define KERN_BASE LOADER_KERN_BASE

/* 사용자 스택의 시작점입니다. */
#define USER_STACK 0x47480000

/* VADDR이 사용자 가상 주소이면 true를 반환합니다. */
#define is_user_vaddr(vaddr) (!is_kernel_vaddr((vaddr)))

/* VADDR이 커널 가상 주소이면 true를 반환합니다. */
#define is_kernel_vaddr(vaddr) ((uint64_t)(vaddr) >= KERN_BASE)

// FIXME: 검사를 추가해야 합니다.
/* 물리 주소 PADDR이 매핑된 커널 가상 주소를 반환합니다. */
#define ptov(paddr) ((void *) (((uint64_t) paddr) + KERN_BASE))

/* 커널 가상 주소 VADDR이 매핑된 물리 주소를 반환합니다. */
#define vtop(vaddr) \
({ \
	ASSERT(is_kernel_vaddr(vaddr)); \
	((uint64_t) (vaddr) - (uint64_t) KERN_BASE);\
})

#endif /* threads/vaddr.h 끝 */
