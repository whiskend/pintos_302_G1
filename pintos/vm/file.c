/* file.c: 메모리 기반 파일 객체(mmap된 객체)의 구현. */

#include <string.h>
#include "vm/vm.h"
#include "threads/mmu.h"
#include "userprog/process.h"
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

static struct bitmap *swap_bit;
static struct lock swap_lock;

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
	struct file_page *file_page = &page->file;
	if (file_page->swapped) {
		lock_acquire(&swap_lock);
		// bitmap_reset(swap_bit, file_page->index);
		file_page->swapped = false;
		lock_release(&swap_lock);
	}
}

static bool
lazy_load_segment (struct page *page, void *aux) {
	struct lazy_aux *lazy = (struct lazy_aux *) aux;
	
	if(page->frame->kva == NULL)
		printf("kva NULL\n");
	
	if (file_read_at (lazy->file, page->frame->kva, lazy->read_bytes, lazy->offset) != (int) lazy->read_bytes) {
		printf("file read 실패\n");
		return false;
	}
	memset ((uint8_t *) page->frame->kva + lazy->read_bytes, 0, lazy->zero_bytes);

	return true;
}


/* 
mmap을 수행합니다. 
length bytes를 fd로 열린 파일에서 offset byte부터 프로세스(process)의 가상 주소 공간 addr에 매핑합니다. 전체 파일은 addr에서 시작하는 연속적인 가상 페이지에 매핑됩니다. 파일 길이가 PGSIZE의 배수가 아니면 마지막으로 매핑된 페이지의 일부 bytes가 파일 끝을 넘어 "삐져나옵니다". 이 페이지에서 페이지 폴트(page fault)가 발생해 메모리로 읽어 들일 때 해당 bytes를 0으로 설정하고, 페이지를 디스크에 다시 쓸 때는 버립니다. 성공하면 이 함수는 파일이 매핑된 가상 주소를 반환합니다. 실패하면 파일 매핑에 유효한 주소가 아닌 NULL을 반환해야 합니다.
*/
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	if (addr == NULL || file == NULL || length == 0)
			return NULL;

	file = file_reopen(file);

	void *start = addr;
	uint32_t read_bytes = file_length(file) - offset;
	uint32_t zero_bytes = 0;
	if (length % PGSIZE != 0) {
		zero_bytes = PGSIZE - length % PGSIZE;
	}
	
	if ((read_bytes + zero_bytes) % PGSIZE != 0 || pg_ofs (addr) != 0 || offset % PGSIZE != 0){
		file_close(file);
		return NULL;
	}

	while (read_bytes > 0 || zero_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct lazy_aux *aux = malloc(sizeof *aux); // lazy_load_segment에 정보를 전달하도록 aux를 설정
		aux->file = file;
		aux->offset = offset;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;

		if (!vm_alloc_page_with_initializer (VM_FILE, addr, writable, lazy_load_segment, aux))
			return NULL;

		/* 다음 페이지로 이동 */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}

	return start;
}

/* munmap을 수행합니다. */
void
do_munmap (void *addr) {
	// munmap은 >>페이지<< 를 해제하는 함수임. munmap이 호출되면, 해당 페이지가 SPT에서 제거되고, 페이지가 점유한 프레임이 해제되어야 함.
	struct supplemental_page_table *spt = &thread_current()->spt;
	
	// 1. 일단 addr 기반으로 SPT에서 페이지를 찾음.
	struct page *page = spt_find_page(spt, addr);
	if (page == NULL) {
		return; // 페이지가 없으면 munmap할 필요가 없당
	}
	// 2. VM_FILE인지 check. 근데 lazy한 상태라면 아직 VM_UNINIT 일 수 있음. 이에 타입 확인해야 함.
	if (page_get_type (page) != VM_FILE) {
		spt_remove_page(spt, page);
		free(page->aux);
		return;
	}
	
	while (page != NULL && page_get_type (page) == VM_FILE) {
		void *next_va = page->va + PGSIZE;
		
		// 3. memory에 올라와 있는지 check. page->frame이 NULL이면 아직 lazy 상태라 실제 frame은 없는 상태다.
		if (page->frame != NULL) {
			struct frame *frame = page->frame;
			
			// 4. dirty한 페이지면 파일에 refresh 해줘야 함.
			file_backed_swap_out (page);

			// 5. pml4 매핑 제거함
			pml4_clear_page(thread_current ()->pml4, page->va);

			// 6. frame table 에서 제거함. 실제 물리페이지 해제.,
			list_remove (&frame->elem);
			palloc_free_page (frame->kva);

			frame->page = NULL;
			page->frame = NULL;

			free(frame);
		}
		// 7. SPT에서 제거함.
		spt_remove_page(spt, page);
		// 8. 여러 페이지가 있으니깐.. 다음 mmap page로,,,
		page = spt_find_page (spt, next_va);
	}
}