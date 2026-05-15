/* anon.c: 디스크 이미지가 없는 페이지(익명 페이지)의 구현. */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"

/* 아래 줄은 수정하지 마세요. */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* 이 구조체는 수정하지 마세요. */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* 익명 페이지에 필요한 데이터를 초기화합니다. */
void
vm_anon_init (void) {
	/* TODO: swap_disk를 설정합니다. */
	swap_disk = NULL;
}

/* 파일 매핑을 초기화합니다. */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* 핸들러를 설정합니다. */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	// bool 이니깐.. 일단 return true..
	return true;
}

/* 스왑 디스크에서 내용을 읽어 페이지를 스왑 인합니다. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	// stack page는 anon page로 만들어져야 하는디, stack page의 초기값은 0으로 채워져야 함.
	// 그래서 내용을 채워주는 swap_in에서 frame을 처음 할당 받는 순간에 0으로 채워줘야 함.
	// 그런데 아직 anon 을 구현할 단계(?)는 아니기에 우선 구현을 대충 해둡니다요.
	// 나중에 swap out도 구현을 하면, swap 한 걸 따로 빼둔 swap slot이 생길건데,
	// swap slot == null 인 경우가 frame을 처음 할당 받은 상태이므로, if로 구분.. 해줘야 함니다요.
	memset (kva, 0, PGSIZE);
	// bool 이니깐.. 일단 return true..
	return true;
}

/* 스왑 디스크에 내용을 써서 페이지를 스왑 아웃합니다. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}

/* 익명 페이지를 파괴합니다. PAGE는 호출자가 해제합니다. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
