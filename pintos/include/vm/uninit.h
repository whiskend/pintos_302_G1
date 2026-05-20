#ifndef VM_UNINIT_H
#define VM_UNINIT_H
#include "vm/vm.h"

struct page;
enum vm_type;

typedef bool vm_initializer (struct page *, void *aux);

/* 초기화되지 않은 페이지. "지연 로딩"을 구현하기 위한 타입입니다. */
struct uninit_page {
	/* 페이지 내용을 초기화합니다. */
	vm_initializer *init;
	enum vm_type type;
	void *aux;
	bool (*page_initializer) (struct page *, enum vm_type, void *kva); // uninit이던 page를 진짜 page로 바꾸는 함수: anon_initializer() -> 이 page는 이제 anon page다 -> page->operations = &anon_ops
};

void uninit_new (struct page *page, void *va, vm_initializer *init,
		enum vm_type type, void *aux,
		bool (*initializer)(struct page *, enum vm_type, void *kva));
#endif
