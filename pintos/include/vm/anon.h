#ifndef VM_ANON_H
#define VM_ANON_H
#include <stdbool.h>
#include <stddef.h>
#include <bitmap.h>
struct page;
enum vm_type;

struct anon_page {
    // bitmap 번호
    size_t index;
    bool swapped;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
