/* file.c: 메모리 기반 파일 객체(mmap된 객체)의 구현. */

#include <string.h>
#include "vm/vm.h"
#include "vm/file.h"
#include "threads/mmu.h"
#include "threads/thread.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* 이 구조체는 수정하지 마세요. */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* 파일 VM 초기화 함수입니다. */
void
vm_file_init (void) {
}

/* 파일 기반 페이지를 초기화합니다. */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* 핸들러를 설정합니다. */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
	file_page->swapped = false;
	return true;
}

/* 파일에서 내용을 읽어 페이지를 스왑 인합니다. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	if(file_read_at (page->aux->file, page->frame->kva, page->aux->read_bytes, page->aux->offset)
	 != (int)page->aux->read_bytes) {
		return false;
	}
	memset ((uint8_t *) page->frame->kva + page->aux->read_bytes, 0,
			page->aux->zero_bytes);

	return true;
}

/* 파일에 내용을 다시 써서 페이지를 스왑 아웃합니다. */
static bool
file_backed_swap_out (struct page *page) {
	uint64_t *pml4 = page->frame->owner->pml4;

	if (!pml4_is_dirty(pml4, page->va)) {
		return true;
	}

	if (file_write_at(page->aux->file, page->frame->kva,
		page->aux->read_bytes, page->aux->offset) != (int) page->aux->read_bytes)
		return false;

	return true;
}

/* 파일 기반 페이지를 파괴합니다. PAGE는 호출자가 해제합니다. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* mmap을 수행합니다. */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
}

/* munmap을 수행합니다. */
void
do_munmap (void *addr) {
}
