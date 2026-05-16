/* anon.c: 디스크 이미지가 없는 페이지(익명 페이지)의 구현. */

#include "vm/vm.h"
#include "vm/anon.h"
#include "devices/disk.h"

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

static struct bitmap *swap_bit;

/* 익명 페이지에 필요한 데이터를 초기화합니다. */
void
vm_anon_init (void) {
	/* TODO: swap_disk를 설정합니다. */
	swap_disk = disk_get(1 , 1);
	ASSERT(swap_disk);
	swap_bit = bitmap_create(disk_size(swap_disk) / PGSIZE);
	ASSERT(swap_bit);
}

/* 파일 매핑을 초기화합니다. */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	if (page == NULL || kva == NULL || type != VM_ANON)
		return false;

	/* 핸들러를 설정합니다. */
	page->operations = &anon_ops;
	struct anon_page *anon_page = &page->anon;
	if(anon_page == NULL)
		return false;
	anon_page->index = 0;
	anon_page->swapped = false;
	// anon_page가 무엇을 갖고 있는가?
	// bit 초기화하고 anon_page에 저장
	return true;
}

/* 스왑 디스크에서 내용을 읽어 페이지를 스왑 인합니다. */
static bool
anon_swap_in (struct page *page, void *kva) {
	if (page == NULL || kva == NULL)
		return false;

	struct anon_page *anon_page = &page->anon;
	// 쫓겨났던 데이터가 다시 필요해지면, 기록해 둔 슬롯 번호를 보고 스왑 디스크에서 메모리로 데이터를 읽어온다.
	if(anon_page == NULL || !anon_page->swapped)
		return false;

	// TODO: 세 번째 인자 Buffer 자리에 kva가 오는게 맞나? 확인 필요
	disk_read(swap_disk, anon_page->index, kva);
	// 그리고 비트맵은 비었다고 다시 표시 -> 더티 비트 0로 활성화
	bitmap_reset(swap_bit, anon_page->index);

	anon_page->swapped = false;
}

/* 스왑 디스크에 내용을 써서 페이지를 스왑 아웃합니다. */
static bool
anon_swap_out (struct page *page) {
	if (page == NULL)
		return false;

	struct anon_page *anon_page = &page->anon;
	if (anon_page == NULL || anon_page->swapped)
		return false;
	
	// 메모리가 꽉차서 페이지를 쫓아낼 때 사용
	// 비트맵을 확인해서 빈 스왑 슬롯을 찾는다.
	size_t idx;

	if (!(idx = bitmap_scan(swap_bit, 0, 1, false)))
		return false;
	
	// 빈 자리를 찾으면 스왑 디스크의 해당 위치에 데이터를 복사
	// TODO: 세 번째 인자 Buffer 자리에 kva가 오는게 맞나? 확인 필요
	disk_write(swap_disk, idx, page->frame->kva);
	// 나중에 다시 찾을 수 있도록 페이지 안에 슬롯 번호를 기록
	bitmap_mark(swap_bit, idx);
	
	anon_page->index = idx;
	anon_page->swapped = true;
}

/* 익명 페이지를 파괴합니다. PAGE는 호출자가 해제합니다. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	size_t idx;

	if ((idx = bitmap_scan(swap_bit, 0, 1, false))) {
		bitmap_reset(swap_bit, idx);
	}
		
	free(anon_page);
}