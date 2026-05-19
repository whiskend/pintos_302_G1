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
#include "threads/interrupt.h"

struct lock frame_table_lock;
struct list frame_table;

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
static void rollback_frame (struct page *page, struct frame *frame);

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
		bool (*initializer)(struct page *, enum vm_type, void *);
		if (type == VM_ANON) {
			initializer = anon_initializer;
		}
		else if (type == VM_FILE) {
			initializer = file_backed_initializer;
		}

		// 수정.. page -> *page
		// why? struct page 하나를 넣어야 함. 그런데 포인터 크기 만큼만 잡고 있어요.
		// 까딱하면 여기서 부터 터질 듯...
		struct page *page = malloc(sizeof (struct page));
		if(page == NULL) {
			return false;
			printf("page malloc 실패\n");
		}

		// if(aux != NULL) {
		// 	page->aux = *(struct lazy_aux *) aux;
		// }
		
		// 추가..
		// why? 읽기 전용인지, 쓰기가 가능한지 나중에 fault 처리할 때 봐야하는데, 나중에 보면 모를 거 같아서 일단 넣어둡니다.
		uninit_new(page, upage, init, type, aux, initializer);
		page->writable = writable;
		/* TODO: 페이지를 spt에 삽입합니다. */
		// 수정 *spt -> spt
		// why? 페이지 목록의 주소를 넘겨야 하는데, 그 주소의 주소를 넘김. 잘못 넘긴 꼴..
		if(spt_insert_page(spt, page)) {
			return true;
		}
		else {

			printf("spt_insert_page 실패\n");
		}
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
	{
		return NULL;
		printf("hash_find 실패\n");

	}
	return hash_entry(e, struct page, hash_elem);
}

/* 검증 후 PAGE를 spt에 삽입합니다. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page) {
	bool success = false;
	/* TODO: 이 함수를 채웁니다. */
	//해당 가상 주소가 주어진 보조 페이지 테이블에 존재하지 않는지 확인해야함.
	//hash_insert를 참고해보자.
	//page의 hash_elem과 spt의 hash_elem 비교.
	if(hash_insert(spt->hash_pages, &page->hash_elem) == NULL) {
		success = true;
	}
	else {
		success = false;
		printf("hash_insert 실패\n");
	}
	//hash_entry()
	return success;
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
	 /* TODO: 축출 정책은 직접 정합니다. */
	for (struct list_elem *e = list_begin (&frame_table);
	e != list_end (&frame_table); e = list_next (e)) {
		struct frame *temp = list_entry(e, struct frame, elem);
		if (temp->page == NULL || temp->owner == NULL)
			continue;
		if (pml4_is_accessed(temp->owner->pml4, temp->page->va)) {
			pml4_set_accessed(temp->owner->pml4, temp->page->va, false);
		}
		else {
			return temp;
		}
	}

	for (struct list_elem *e = list_begin (&frame_table);
	e != list_end (&frame_table); e = list_next (e)) {
		struct frame *temp = list_entry(e, struct frame, elem);
		if (temp->page != NULL && temp->owner != NULL)
			return temp;
	}
	return NULL;
}

/* 페이지 하나를 축출하고 해당 프레임을 반환합니다.
 * 오류 시 NULL을 반환합니다. */
static struct frame *
vm_evict_frame (void) {
	lock_acquire(&frame_table_lock);
	struct frame *victim = vm_get_victim ();
	ASSERT(victim != NULL);
	list_remove(&victim->elem);
	lock_release(&frame_table_lock);

	/* TODO: victim을 스왑 아웃하고 축출된 프레임을 반환합니다. */
	if (!swap_out(victim->page)) {
		lock_acquire(&frame_table_lock);
		list_push_back(&frame_table, &victim->elem);
		lock_release(&frame_table_lock);
		return NULL;
	}

	// pml4 페이지 클리어
	pml4_clear_page(victim->owner->pml4, victim->page->va);
	victim->page->frame = NULL;
	victim->page = NULL;
	victim->owner = NULL;

	// 프레임 
	return victim;
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
		frame = vm_evict_frame();
	}
	else {
		frame = malloc (sizeof *frame);
	
		if (frame == NULL) {
			palloc_free_page(kva);
			return NULL;
		}
		frame->kva = kva;
		frame->page = NULL;
		frame->owner = NULL;
	}

	if (frame == NULL)
		return NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	lock_acquire(&frame_table_lock);
	list_push_back(&frame_table, &frame->elem);
	lock_release(&frame_table_lock);

	return frame;
}

/* 스택을 확장합니다. */
/* syscall.c에서 써야 해서 static 제거 */
void
vm_stack_growth (void *addr) {
	// fault가 난 주소를 기점으로 페이지 경계로 내리기.
	void *upage = pg_round_down(addr);
	// 디버깅용
	// printf ("vm_stack_growth: addr=%p upage=%p\n", addr, upage);

	// bool ok = vm_alloc_page (VM_ANON, upage, true);
	// printf ("vm_alloc_page stack result=%d\n", ok);
	// 그리고 그 주소에다가 ANON 페이지 만들기.
	vm_alloc_page (VM_ANON, upage, true);
	// 만든 페이지를 바로 CLAIM하기.. 해줘야 하는데,
	// vm_try_handle_fault()에서도 claim을 해주니깐.. 여기서 지움.
	// vm_claim_page (upage);
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
	void *rsp;

	if (user) {
		// 유저모드에서 fault 났을 때.
		rsp = (void *) f->rsp;
		/*디버깅용 코드*/
		// printf("user mode fault, rsp: %p\n", rsp);
	} else {
		// 커널모드에서 fault 났을 때.
		// syscall 처리 중에 유저 주소 건들면 fault가 날 수 잇음..
		// 근데 f->rsp는 커널 스택 포인터라... stack growth(유저 스택 늘리기..라서..) 판단에 쓰면 안 된다.
		// 그래서 syscall 진입할 때 user_rsp 저장해뒀다가 씀.
		rsp = (void *) thread_current()->user_rsp;
		/*디버깅용 코드*/
		// printf ("fault addr=%p user=%d write=%d not_present=%d rsp=%p\n",
        // addr, user, write, not_present, rsp);
	}
	/* TODO: 폴트를 검증합니다. */
	/* TODO: 여기에 코드를 작성합니다. */

	if (addr == NULL) {
		// printf ("addr is NULL\n");
		goto fail;
	}
	if (is_kernel_vaddr(addr)) {
		// printf ("addr is kernel vaddr\n");
		goto fail;
	}
	if (!not_present) {
		// printf ("page is present\n");
		goto fail;
	}		
	page = spt_find_page(spt, addr);
	// printf ("spt_find_page: %p\n", page);
	if (page == NULL) {
		// 다음 조건을 만족해야 stack_growth로 넘어감.
		if (addr != NULL &&
		// 1. addr가 user addr인지?
		is_user_vaddr(addr) &&
		// 2. addr가 USER_STACK 아래인지?
		addr < USER_STACK &&
		// 3. 너무 많이 자라지는 않았는지?
		addr >= USER_STACK - STACK_MAX &&
		// 4. addr >= rsp - 8 정도인지?
		addr >= rsp - 8 ) {
		// 하나라도 만족 못하면 goto fail.
			vm_stack_growth (addr);	
			page = spt_find_page (&thread_current()->spt, addr);
		} else {
			// printf("Stack growth conditions not met\n");
			goto fail;
		}
	}
	if (write && !page->writable) {
		// printf ("write access to read-only page\n");
		goto fail;
	}

	return vm_do_claim_page (page);
	fail:
		return false;
}

static void rollback_frame (struct page *page, struct frame *frame) {
	frame->page = NULL;
	frame->owner = NULL;
	page->frame = NULL;

	lock_acquire(&frame_table_lock);
	list_remove(&frame->elem);
	lock_release(&frame_table_lock);

	//palloc_free_page(frame->kva);

	free(frame);
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
	page = spt_find_page(&thread_current()->spt, va);
	
	if (page == NULL) {
		return false;
	}

	return vm_do_claim_page (page);
}

/* PAGE를 claim하고 MMU를 설정합니다. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	if(frame == NULL)
		return false;

	/* 링크를 설정합니다. */
	frame->page = page;
	frame->owner = thread_current();
	page->frame = frame;

	/* TODO: 페이지의 VA를 프레임의 PA에 매핑하는 페이지 테이블 엔트리를 삽입합니다. */
	struct thread *t = thread_current();
	if(!pml4_set_page(t->pml4, page->va, frame->kva, page->writable)){
		rollback_frame(page, frame);
		return false;
	}
	
	if(swap_in (page, frame->kva))
		return true;
	else {
		pml4_clear_page(t->pml4, page->va);
		rollback_frame(page, frame);
		return false;
	}
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

static void
spt_destroy_page (struct hash_elem *e, void *aux) {
	struct page *page = hash_entry (e, struct page, hash_elem);

	if (page->frame != NULL) {
		struct frame *frame = page->frame;
		
		pml4_clear_page (thread_current()->pml4, page->va);

		lock_acquire (&frame_table_lock);
		list_remove (&frame->elem);
		lock_release (&frame_table_lock);

		palloc_free_page (frame->kva);

		frame->page = NULL;
		page->frame = NULL;

		free (frame);
		}
	vm_dealloc_page (page);
}

/* 보조 페이지 테이블이 보유한 자원을 해제합니다. */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: 스레드가 보유한 모든 supplemental_page_table을 파괴하고,
	 * TODO: 수정된 모든 내용을 저장소에 다시 씁니다. */
	hash_destroy (spt->hash_pages, spt_destroy_page);
	free (spt->hash_pages);
}
