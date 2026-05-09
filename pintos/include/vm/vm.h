#ifndef VM_VM_H
#define VM_VM_H
#include <stdbool.h>
#include "threads/palloc.h"

enum vm_type {
	/* 초기화되지 않은 페이지 */
	VM_UNINIT = 0,
	/* 파일과 관련 없는 페이지, 즉 익명 페이지 */
	VM_ANON = 1,
	/* 파일과 관련된 페이지 */
	VM_FILE = 2,
	/* 프로젝트 4에서 페이지 캐시를 담는 페이지 */
	VM_PAGE_CACHE = 3,

	/* 상태를 저장하기 위한 비트 플래그 */

	/* 정보를 저장하기 위한 보조 비트 플래그 마커입니다. 값이 int에 들어가는 한
	 * 마커를 더 추가할 수 있습니다. */
	VM_MARKER_0 = (1 << 3),
	VM_MARKER_1 = (1 << 4),

	/* 이 값을 넘지 마세요. */
	VM_MARKER_END = (1 << 31),
};

#include "vm/uninit.h"
#include "vm/anon.h"
#include "vm/file.h"
#ifdef EFILESYS
#include "filesys/page_cache.h"
#endif

struct page_operations;
struct thread;

#define VM_TYPE(type) ((type) & 7)

struct list frame_table;

/* "page"의 표현입니다.
 * 이는 일종의 "부모 클래스"이며, uninit_page, file_page, anon_page,
 * 페이지 캐시(프로젝트 4)라는 네 가지 "자식 클래스"를 가집니다.
 * 이 구조체의 미리 정의된 멤버는 제거하거나 수정하지 마세요. */
struct page {
	const struct page_operations *operations;
	void *va;              /* 사용자 공간 기준 주소 */
	struct frame *frame;   /* 프레임에 대한 역참조 */

	/* 직접 구현할 부분 */

	/* 타입별 데이터는 union에 묶여 있습니다.
	 * 각 함수는 현재 union을 자동으로 감지합니다. */
	union {
		struct uninit_page uninit;
		struct anon_page anon;
		struct file_page file;
#ifdef EFILESYS
		struct page_cache page_cache;
#endif
	};

	// SPT를 순회하기 위한 해시 자료구조
	struct hash_elem *e;
	
	// 첫 페이지 폴트가 되어 있는지 확인하는 불 변수
	// 읽기만 가능한 곳에 쓰기를 하면 비정상적인 페이지 폴트
	// 해당 사항은 페이지 초기화 시 설정
	bool writable;
};

/* "frame"의 표현입니다. */
struct frame {
	void *kva;
	struct page *page;
	struct list_elem *e;
};

struct lazy_aux {
	struct file *file;
	off_t offset;
	uint32_t read_bytes;
	uint32_t zero_bytes;
};

/* 페이지 연산을 위한 함수 테이블입니다.
 * 이는 C에서 "인터페이스"를 구현하는 한 가지 방법입니다.
 * "메서드" 테이블을 구조체 멤버에 넣고 필요할 때마다 호출합니다. */
struct page_operations {
	bool (*swap_in) (struct page *, void *);
	bool (*swap_out) (struct page *);
	void (*destroy) (struct page *);
	enum vm_type type;
};

#define swap_in(page, v) (page)->operations->swap_in ((page), v)
#define swap_out(page) (page)->operations->swap_out (page)
#define destroy(page) \
	if ((page)->operations->destroy) (page)->operations->destroy (page)

/* 현재 프로세스 메모리 공간의 표현입니다.
 * 이 구조체에 대해 특정 설계를 강제하지 않습니다.
 * 모든 설계는 직접 정하면 됩니다. */
struct supplemental_page_table {
	// 시작 주소만 있고 실행 내역이 없을 때, 진짜 페이지 폴트인지 여부 체크
	// 스왑 아웃 쪽에 있는지? 레이지 로딩을 해야하는지? 아예 안올라와 있는지?
	// 스왑 아웃에 있으면 스왑 인

};

#include "threads/thread.h"
void supplemental_page_table_init (struct supplemental_page_table *spt);
bool supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src);
void supplemental_page_table_kill (struct supplemental_page_table *spt);
struct page *spt_find_page (struct supplemental_page_table *spt,
		void *va);
bool spt_insert_page (struct supplemental_page_table *spt, struct page *page);
void spt_remove_page (struct supplemental_page_table *spt, struct page *page);

void vm_init (void);
bool vm_try_handle_fault (struct intr_frame *f, void *addr, bool user,
		bool write, bool not_present);

#define vm_alloc_page(type, upage, writable) \
	vm_alloc_page_with_initializer ((type), (upage), (writable), NULL, NULL)
bool vm_alloc_page_with_initializer (enum vm_type type, void *upage,
		bool writable, vm_initializer *init, void *aux);
void vm_dealloc_page (struct page *page);
bool vm_claim_page (void *va);
enum vm_type page_get_type (struct page *page);

#endif  /* VM_VM_H */
