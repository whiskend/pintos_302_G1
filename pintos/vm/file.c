/* file.c: 메모리 기반 파일 객체(mmap된 객체)의 구현. */

#include "vm/vm.h"

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
	struct file_page *file_page UNUSED = &page->file;
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
	ASSERT(addr == NULL);
	ASSERT(file == NULL);
	ASSERT(length == 0);
	/* length bytes를 fd로 열린 파일에서 offset byte부터 프로세스(process)의 가상 주소 공간 addr에 매핑합니다. 전체 파일은 addr에서 시작하는 연속적인 가상 페이지에 매핑됩니다. 파일 길이가 PGSIZE의 배수가 아니면 마지막으로 매핑된 페이지의 일부 bytes가 파일 끝을 넘어 "삐져나옵니다". 이 페이지에서 페이지 폴트(page fault)가 발생해 메모리로 읽어 들일 때 해당 bytes를 0으로 설정하고, 페이지를 디스크에 다시 쓸 때는 버립니다. 성공하면 이 함수는 파일이 매핑된 가상 주소를 반환합니다. 실패하면 파일 매핑에 유효한 주소가 아닌 NULL을 반환해야 합니다. */
	
	if (!vm_alloc_page(VM_FILE, addr, writable)) {
		return;
	}
	if (length < PGSIZE) {
		zero_bytes = PGSIZE - length;
	}
}

/* munmap을 수행합니다. */
void
do_munmap (void *addr) {
}
