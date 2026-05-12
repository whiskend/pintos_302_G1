/* 
vm_claim_page() -> 주소 기준 claim 요청 진입점

spt_find_page() -> 주소에 해당하는 page metadata 찾기

vm_do_claim_page() -> claim 실제 처리 총괄

vm_get_frame() -> 빈 frame 준비

palloc_get_page(PAL_USER) ->> user pool에서 실제 4KB 메모리 할당

frame table insert -> 확보한 frame을 전역 관리 목록에 등록 -> 헬퍼로 빼야할듯? remove도..

page <-> frame 연결 -> 가상 page와 실제 frame 관계 확정

pml4_set_page() -> CPU 번역표에 매핑 추가

swap_in() -> frame에 page 내용 채우기

rollback -> 실패 시 원상복구
*/
/* vm.c: 가상 메모리 객체를 위한 일반 인터페이스. */

#include "threads/malloc.h"
#include "threads/mmu.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "kernel/hash.h"
#include "threads/vaddr.h"

/* 각 서브시스템의 초기화 코드를 호출하여 가상 메모리 서브시스템을 초기화합니다. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
	list_init(&frame_table);
	lock_init(&frame_table_lock);
#ifdef EFILESYS  /* 프로젝트 4용 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* 위 줄들은 수정하지 마세요. */
	/* TODO: 여기에 코드를 작성합니다. */
	// 프레임 테이블이랑 락 초기화 필요.
}

/* 페이지의 타입을 얻습니다. 페이지가 초기화된 뒤의 타입을 알고 싶을 때 유용합니다.
 * 이 함수는 현재 완전히 구현되어 있습니다. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* 헬퍼 함수들 */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* 초기화 함수가 있는 대기 중인 페이지 객체를 생성합니다. 페이지를 만들려면
 * 직접 생성하지 말고 이 함수나 `vm_alloc_page`를 통해 생성하세요. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* upage가 이미 사용 중인지 확인합니다. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: 페이지를 생성하고 VM 타입에 맞는 initializer를 가져온 뒤,
		 * TODO: uninit_new를 호출하여 "uninit" 페이지 구조체를 생성합니다.
		 * TODO: uninit_new 호출 뒤 필드를 수정해야 합니다. */
		struct page *page = malloc(sizeof page);
		uninit_new(page, upage, init, type, aux, ())
	/* TODO: 페이지를 spt에 삽입합니다. */
		//if(spt_insert_page(&spt))
	}
err:
	return false;
}

/* spt에서 VA를 찾아 페이지를 반환합니다. 오류 시 NULL을 반환합니다. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	struct page page;
	/* TODO: 이 함수를 채웁니다. */
	struct hash_elem *e;
	page.va = pg_round_down(va);
	e = hash_find (spt->hash_pages, &page.hash_elem);
	if (e == NULL)
		return NULL;
	return hash_entry(e, struct page, hash_elem);
}

/* 검증 후 PAGE를 spt에 삽입합니다. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page) {
	bool succ = false;
	/* TODO: 이 함수를 채웁니다. */
	//해당 가상 주소가 주어진 보조 페이지 테이블에 존재하지 않는지 확인해야함.
	//hash_insert를 참고해보자.
	//page의 hash_elem과 spt의 hash_elem 비교.
	if(hash_insert(spt->hash_pages, &page->hash_elem) == NULL) {
		succ = true;
	}
	else {
		succ = false;
	}
	//hash_entry()
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	//page내의 포인터들을 free 시켜줘야 함.
	hash_delete(spt->hash_pages, &page->hash_elem);
	free(page->frame);
	//이건 원래 있던 거.
	vm_dealloc_page (page);
	return true;
}

/* 축출할 struct frame을 가져옵니다. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: 축출 정책은 직접 정합니다. */

	return victim;
}

/* 페이지 하나를 축출하고 해당 프레임을 반환합니다.
 * 오류 시 NULL을 반환합니다. */
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: victim을 스왑 아웃하고 축출된 프레임을 반환합니다. */

	return NULL;
}

/* palloc()을 호출해 프레임을 가져옵니다. 사용 가능한 페이지가 없으면 페이지를
 * 축출하고 반환합니다. 이 함수는 항상 유효한 주소를 반환합니다. 즉, 사용자 풀
 * 메모리가 가득 차면 사용 가능한 메모리 공간을 얻기 위해 프레임을 축출합니다. */
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: 이 함수를 채웁니다. */
	
	void *kva = palloc_get_page(PAL_USER);
	if (kva == NULL) {
		return NULL;
	}

	frame = malloc (sizeof *frame);
	// 할당이 안 된 경우... 롤백을 여기서 해야할듯여
	// FRAME 할당 초기화,,
	if (frame == NULL) {
		palloc_free_page(kva);
		return NULL;
	}
	frame->kva = kva;
	frame->page = NULL;
	lock_acquire(&frame_table_lock);
	list_push_back(&frame_table, &frame->e);
	lock_release(&frame_table_lock);

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* 스택을 확장합니다. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* 쓰기 보호된 페이지에서 발생한 폴트를 처리합니다. */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* 성공 시 true를 반환합니다. */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write , bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: 폴트를 검증합니다. */
	/* TODO: 여기에 코드를 작성합니다. */

	
	if (addr == NULL)
		goto done;
	if (is_kernel_vaddr(addr))
		goto done;
	if(!not_present)
		goto done;
	page = spt_find_page(spt, addr);
	if(page == NULL)
		goto done;
	if(write && !page->writable)
			goto done;

	return vm_do_claim_page (page);
	done:
		exit(-1);
}

/* 페이지를 해제합니다.
 * 이 함수는 수정하지 마세요. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* VA에 할당된 페이지를 claim합니다. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: 이 함수를 채웁니다. */
	/* 
	- va를 pg_round_down으로 page boundary에 맞춘다 -> spt_find_page()에 되어있음.
    - 현재 thread의 spt에서 spt_find_page()로 page를 찾는다
    - 없으면 false 반환
    - 있으면 vm_do_claim_page(page) 호출
	*/
	page = spt_find_page(thread_current()->spt, va);
	// 예외처리
	if (page == NULL) {
		return false;
	}
	return vm_do_claim_page (page);
}

/* PAGE를 claim하고 MMU를 설정합니다. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* 링크를 설정합니다. */
	frame->page = page;
	page->frame = frame;

	/* TODO: 페이지의 VA를 프레임의 PA에 매핑하는 페이지 테이블 엔트리를 삽입합니다. */
	struct thread *t = thread_current();
	if(!pml4_set_page(t->pml4, page->va, frame->kva, page->writable)){
		pml4_clear_page(t->pml4, page->va);
		// vm_dealloc_page..로 해줘야 할듯?
		vm_dealloc_page(page);
		// frame도 free해주고...
		free(frame);
		// page도...?
		frame->page = NULL;
		page->frame = NULL;
		return false;
	}
	
	return swap_in (page, frame->kva);
}

static bool hash_va_less(const struct hash_elem *a,
		const struct hash_elem *b,
		void *aux)
{
	struct page *page_a = hash_entry(a, struct page, hash_elem);
	struct page *page_b = hash_entry(b, struct page, hash_elem);

	return page_a->va > page_b->va;
}

static uint64_t hash_func(const struct hash_elem *e, void *aux) {
	const struct page *p = hash_entry (e, struct page, hash_elem);
	return hash_bytes (&p->va, sizeof p->va);
}

/* 새 보조 페이지 테이블을 초기화합니다. */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	spt->hash_pages = malloc(sizeof *spt->hash_pages);
	ASSERT(spt->hash_pages != NULL);
	bool suc = hash_init(spt->hash_pages, hash_func, hash_va_less, NULL);
	ASSERT(suc);
}

/* 보조 페이지 테이블을 src에서 dst로 복사합니다. */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* 보조 페이지 테이블이 보유한 자원을 해제합니다. */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: 스레드가 보유한 모든 supplemental_page_table을 파괴하고,
	 * TODO: 수정된 모든 내용을 저장소에 다시 씁니다. */
}
