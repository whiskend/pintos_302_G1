/* file.c: 메모리 기반 파일 객체(mmap된 객체)의 구현. */

#include "vm/vm.h"
#include "threads/mmu.h"
#include "userprog/process.h"

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
}

/* 파일에서 내용을 읽어 페이지를 스왑 인합니다. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page
	UNUSED = &page->file;
}

/* 파일에 내용을 다시 써서 페이지를 스왑 아웃합니다. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
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
	// munmap은 페이지를 해제하는 함수임. munmap이 호출되면, 해당 페이지가 SPT에서 제거되고, 페이지가 점유한 프레임이 해제되어야 함.
	// 1. 일단 addr 기반으로 SPT에서 페이지를 찾음.
	spt_find_page(thread_current()->spt, addr);
	// 2. VM_FILE인지 check
	if (FILE != VM_FILE)
		return false;
	// 3. memory에 올라와 있는지 check
	if (pa != kva)
		return false;
	// 4. pml4 매핑 제거함
	pml4_clear_page();
	// 5. SPT에서 제거함.
	spt_remove_page();
	// 6. page, frame, aux free 해줌.
	free(page);
	free(frame);
	free(aux);
	// 7. mmap용 파일 close
	close(file);
	// 8. process_exit ()
	process_exit();
}