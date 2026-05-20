#include "bitmap.h"
#include <debug.h>
#include <limits.h>
#include <round.h>
#include <stdio.h>
#include "threads/malloc.h"
#ifdef FILESYS
#include "filesys/file.h"
#endif

/* 요소 타입.

   int보다 작지 않은 폭을 가진 부호 없는 정수 타입이어야 한다.

   각 비트는 비트맵의 비트 하나를 나타낸다.
   요소 안의 0번 비트가 비트맵의 K번 비트를 나타낸다면,
   요소 안의 1번 비트는 비트맵의 K+1번 비트를 나타내며,
   이후도 같은 방식이다. */
typedef unsigned long elem_type;

/* 한 요소에 들어 있는 비트 수. */
#define ELEM_BITS (sizeof (elem_type) * CHAR_BIT)

/* 외부에서 볼 때 비트맵은 비트 배열이다. 내부에서는
   위에서 정의한 elem_type 배열로 비트 배열을 흉내 낸다. */
struct bitmap {
	size_t bit_cnt;     /* 비트 수. */
	elem_type *bits;    /* 비트를 표현하는 요소들. */
};

/* BIT_IDX번 비트를 포함하는 요소의 인덱스를 반환한다. */
static inline size_t
elem_idx (size_t bit_idx) {
	return bit_idx / ELEM_BITS;
}

/* BIT_IDX에 해당하는 비트만 켜진 elem_type 값을 반환한다. */
static inline elem_type
bit_mask (size_t bit_idx) {
	return (elem_type) 1 << (bit_idx % ELEM_BITS);
}

/* BIT_CNT개의 비트를 담는 데 필요한 요소 수를 반환한다. */
static inline size_t
elem_cnt (size_t bit_cnt) {
	return DIV_ROUND_UP (bit_cnt, ELEM_BITS);
}

/* BIT_CNT개의 비트를 담는 데 필요한 바이트 수를 반환한다. */
static inline size_t
byte_cnt (size_t bit_cnt) {
	return sizeof (elem_type) * elem_cnt (bit_cnt);
}

/* B의 bits 배열 마지막 요소에서 실제로 사용하는 비트는 1,
   나머지는 0으로 설정된 비트 마스크를 반환한다. */
static inline elem_type
last_mask (const struct bitmap *b) {
	int last_bits = b->bit_cnt % ELEM_BITS;
	return last_bits ? ((elem_type) 1 << last_bits) - 1 : (elem_type) -1;
}

/* 생성과 파괴. */

/* BIT_CNT개의 비트를 가진 비트맵을 만들고,
   모든 비트를 false로 설정한다.
   성공하면 새 비트맵을, 메모리 할당에 실패하면 NULL을 반환한다. */
struct bitmap *
bitmap_create (size_t bit_cnt) {
	struct bitmap *b = malloc (sizeof *b);
	if (b != NULL) {
		b->bit_cnt = bit_cnt;
		b->bits = malloc (byte_cnt (bit_cnt));
		if (b->bits != NULL || bit_cnt == 0) {
			bitmap_set_all (b, false);
			return b;
		}
		free (b);
	}
	return NULL;
}

/* BLOCK에 미리 할당된 BLOCK_SIZE 바이트 저장 공간 안에
   BIT_CNT개의 비트를 가진 비트맵을 만들고 반환한다.
   BLOCK_SIZE는 최소한 bitmap_buf_size(BIT_CNT) 이상이어야 한다. */
struct bitmap *
bitmap_create_in_buf (size_t bit_cnt, void *block, size_t block_size UNUSED) {
	struct bitmap *b = block;

	ASSERT (block_size >= bitmap_buf_size (bit_cnt));

	b->bit_cnt = bit_cnt;
	b->bits = (elem_type *) (b + 1);
	bitmap_set_all (b, false);
	return b;
}

/* BIT_CNT개의 비트를 가진 비트맵을 담는 데 필요한 바이트 수를 반환한다
   (bitmap_create_in_buf()에서 사용). */
size_t
bitmap_buf_size (size_t bit_cnt) {
	return sizeof (struct bitmap) + byte_cnt (bit_cnt);
}

/* 비트맵 B를 파괴하고 저장 공간을 해제한다.
   bitmap_create_in_buf()로 만든 비트맵에는 사용하지 않는다. */
void
bitmap_destroy (struct bitmap *b) {
	if (b != NULL) {
		free (b->bits);
		free (b);
	}
}

/* 비트맵 크기. */

/* B에 들어 있는 비트 수를 반환한다. */
size_t
bitmap_size (const struct bitmap *b) {
	return b->bit_cnt;
}

/* 단일 비트 설정과 검사. */

/* B의 IDX번 비트를 원자적으로 VALUE로 설정한다. */
void
bitmap_set (struct bitmap *b, size_t idx, bool value) {
	ASSERT (b != NULL);
	ASSERT (idx < b->bit_cnt);
	if (value)
		bitmap_mark (b, idx);
	else
		bitmap_reset (b, idx);
}

/* B의 BIT_IDX번 비트를 원자적으로 true로 설정한다. */
void
bitmap_mark (struct bitmap *b, size_t bit_idx) {
	size_t idx = elem_idx (bit_idx);
	elem_type mask = bit_mask (bit_idx);

	/* `b->bits[idx] |= mask'와 같지만, 단일 프로세서 머신에서
	   원자성이 보장된다는 점이 다르다. [IA32-v2b]의 OR 명령
	   설명을 참고하라. */
	asm ("lock orq %1, %0" : "=m" (b->bits[idx]) : "r" (mask) : "cc");
}

/* B의 BIT_IDX번 비트를 원자적으로 false로 설정한다. */
void
bitmap_reset (struct bitmap *b, size_t bit_idx) {
	size_t idx = elem_idx (bit_idx);
	elem_type mask = bit_mask (bit_idx);

	/* `b->bits[idx] &= ~mask'와 같지만, 단일 프로세서 머신에서
	   원자성이 보장된다는 점이 다르다. [IA32-v2a]의 AND 명령
	   설명을 참고하라. */
	asm ("lock andq %1, %0" : "=m" (b->bits[idx]) : "r" (~mask) : "cc");
}

/* B의 IDX번 비트를 원자적으로 토글한다.
   즉 true이면 false로, false이면 true로 바꾼다. */
void
bitmap_flip (struct bitmap *b, size_t bit_idx) {
	size_t idx = elem_idx (bit_idx);
	elem_type mask = bit_mask (bit_idx);

	/* `b->bits[idx] ^= mask'와 같지만, 단일 프로세서 머신에서
	   원자성이 보장된다는 점이 다르다. [IA32-v2b]의 XOR 명령
	   설명을 참고하라. */
	asm ("lock xorq %1, %0" : "=m" (b->bits[idx]) : "r" (mask) : "cc");
}

/* B의 IDX번 비트 값을 반환한다. */
bool
bitmap_test (const struct bitmap *b, size_t idx) {
	ASSERT (b != NULL);
	ASSERT (idx < b->bit_cnt);
	return (b->bits[elem_idx (idx)] & bit_mask (idx)) != 0;
}

/* 여러 비트 설정과 검사. */

/* B의 모든 비트를 VALUE로 설정한다. */
void
bitmap_set_all (struct bitmap *b, bool value) {
	ASSERT (b != NULL);

	bitmap_set_multiple (b, 0, bitmap_size (b), value);
}

/* B에서 START부터 시작하는 CNT개의 비트를 VALUE로 설정한다. */
void
bitmap_set_multiple (struct bitmap *b, size_t start, size_t cnt, bool value) {
	size_t i;

	ASSERT (b != NULL);
	ASSERT (start <= b->bit_cnt);
	ASSERT (start + cnt <= b->bit_cnt);

	for (i = 0; i < cnt; i++)
		bitmap_set (b, start + i, value);
}

/* B에서 START 이상 START + CNT 미만 범위에 있는 비트 중
   VALUE로 설정된 비트 수를 반환한다. */
size_t
bitmap_count (const struct bitmap *b, size_t start, size_t cnt, bool value) {
	size_t i, value_cnt;

	ASSERT (b != NULL);
	ASSERT (start <= b->bit_cnt);
	ASSERT (start + cnt <= b->bit_cnt);

	value_cnt = 0;
	for (i = 0; i < cnt; i++)
		if (bitmap_test (b, start + i) == value)
			value_cnt++;
	return value_cnt;
}

/* B에서 START 이상 START + CNT 미만 범위에 VALUE로 설정된 비트가
   하나라도 있으면 true를, 없으면 false를 반환한다. */
bool
bitmap_contains (const struct bitmap *b, size_t start, size_t cnt, bool value) {
	size_t i;

	ASSERT (b != NULL);
	ASSERT (start <= b->bit_cnt);
	ASSERT (start + cnt <= b->bit_cnt);

	for (i = 0; i < cnt; i++)
		if (bitmap_test (b, start + i) == value)
			return true;
	return false;
}

/* B에서 START 이상 START + CNT 미만 범위에 true로 설정된 비트가
   하나라도 있으면 true를, 없으면 false를 반환한다. */
bool
bitmap_any (const struct bitmap *b, size_t start, size_t cnt) {
	return bitmap_contains (b, start, cnt, true);
}

/* B에서 START 이상 START + CNT 미만 범위에 true로 설정된 비트가
   하나도 없으면 true를, 있으면 false를 반환한다. */
bool
bitmap_none (const struct bitmap *b, size_t start, size_t cnt) {
	return !bitmap_contains (b, start, cnt, true);
}

/* B에서 START 이상 START + CNT 미만 범위의 모든 비트가 true이면
   true를, 아니면 false를 반환한다. */
bool
bitmap_all (const struct bitmap *b, size_t start, size_t cnt) {
	return !bitmap_contains (b, start, cnt, false);
}

/* 설정되었거나 설정되지 않은 비트 찾기. */

/* B에서 START 이후의 위치 중 모든 비트가 VALUE로 설정된
   CNT개의 연속 비트 그룹을 처음으로 찾아 시작 인덱스를 반환한다.
   그런 그룹이 없으면 BITMAP_ERROR를 반환한다. */
size_t
bitmap_scan (const struct bitmap *b, size_t start, size_t cnt, bool value) {
	ASSERT (b != NULL);
	ASSERT (start <= b->bit_cnt);

	if (cnt <= b->bit_cnt) {
		size_t last = b->bit_cnt - cnt;
		size_t i;
		for (i = start; i <= last; i++)
			if (!bitmap_contains (b, i, cnt, !value))
				return i;
	}
	return BITMAP_ERROR;
}

/* B에서 START 이후의 위치 중 모든 비트가 VALUE로 설정된
   CNT개의 연속 비트 그룹을 처음으로 찾아 모두 !VALUE로 뒤집고,
   그 그룹의 첫 번째 비트 인덱스를 반환한다.
   그런 그룹이 없으면 BITMAP_ERROR를 반환한다.
   CNT가 0이면 0을 반환한다.
   비트 설정은 원자적으로 수행되지만, 비트 검사와 설정이
   하나의 원자적 동작으로 묶여 있지는 않다. */
size_t
bitmap_scan_and_flip (struct bitmap *b, size_t start, size_t cnt, bool value) {
	size_t idx = bitmap_scan (b, start, cnt, value);
	if (idx != BITMAP_ERROR)
		bitmap_set_multiple (b, idx, cnt, !value);
	return idx;
}

/* 파일 입출력. */

#ifdef FILESYS
/* B를 파일에 저장하는 데 필요한 바이트 수를 반환한다. */
size_t
bitmap_file_size (const struct bitmap *b) {
	return byte_cnt (b->bit_cnt);
}

/* FILE에서 B를 읽는다. 성공하면 true를, 실패하면 false를 반환한다. */
bool
bitmap_read (struct bitmap *b, struct file *file) {
	bool success = true;
	if (b->bit_cnt > 0) {
		off_t size = byte_cnt (b->bit_cnt);
		success = file_read_at (file, b->bits, size, 0) == size;
		b->bits[elem_cnt (b->bit_cnt) - 1] &= last_mask (b);
	}
	return success;
}

/* B를 FILE에 쓴다. 성공하면 true를, 실패하면 false를 반환한다. */
bool
bitmap_write (const struct bitmap *b, struct file *file) {
	off_t size = byte_cnt (b->bit_cnt);
	return file_write_at (file, b->bits, size, 0) == size;
}
#endif /* FILESYS */

/* 디버깅. */

/* B의 내용을 16진수로 콘솔에 출력한다. */
void
bitmap_dump (const struct bitmap *b) {
	hex_dump (0, b->bits, byte_cnt (b->bit_cnt), false);
}

