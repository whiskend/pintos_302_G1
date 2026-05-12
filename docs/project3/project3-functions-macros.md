# Project 3 구현에 필요한 함수와 매크로 정리

이 문서는 Project 3(Virtual Memory)를 구현할 때 직접 수정하거나 자주 활용해야 하는 함수, 매크로, 구조체를 정리한 것이다. 구현 순서는 보통 `SPT -> lazy loading -> page fault 처리 -> frame/eviction -> mmap/munmap -> fork/cleanup` 흐름으로 잡으면 된다.

## 1. VM 타입과 공통 페이지 인터페이스

위치: `pintos/include/vm/vm.h`, `pintos/vm/vm.c`

| 이름 | 구분 | 설명 |
| --- | --- | --- |
| `enum vm_type` | 매크로성 enum | 페이지 종류를 나타낸다. `VM_UNINIT`, `VM_ANON`, `VM_FILE`, `VM_PAGE_CACHE`가 있으며, Project 3에서는 주로 `VM_UNINIT`, `VM_ANON`, `VM_FILE`을 사용한다. |
| `VM_TYPE(type)` | 매크로 | 타입 값에서 실제 페이지 타입 하위 비트만 뽑는다. marker bit를 섞어 쓰더라도 기본 타입을 얻을 수 있게 한다. |
| `VM_MARKER_0`, `VM_MARKER_1` | enum flag | 페이지에 추가 상태를 표시할 때 쓰는 보조 비트다. 스택 페이지 표시 같은 용도로 활용할 수 있다. |
| `struct page` | 구조체 | 사용자 가상 페이지 하나를 표현한다. `operations`, `va`, `frame`과 타입별 union(`uninit`, `anon`, `file`)을 가진다. SPT에는 보통 이 구조체를 넣는다. |
| `struct frame` | 구조체 | 물리 프레임 하나를 표현한다. `kva`는 커널 가상 주소, `page`는 이 프레임을 점유한 페이지를 가리킨다. eviction 구현 시 frame 목록 관리가 필요하다. |
| `struct page_operations` | 구조체 | 페이지 타입별 동작 테이블이다. `swap_in`, `swap_out`, `destroy`, `type`을 채워서 타입별 다형성처럼 사용한다. |
| `swap_in(page, v)` | 매크로 | 현재 페이지 타입의 `swap_in` 함수를 호출한다. lazy page를 실제 프레임에 올릴 때 사용한다. |
| `swap_out(page)` | 매크로 | 현재 페이지 타입의 `swap_out` 함수를 호출한다. eviction에서 victim page를 저장소로 내릴 때 사용한다. |
| `destroy(page)` | 매크로 | 현재 페이지 타입의 `destroy` 함수를 호출한다. SPT 제거와 프로세스 종료 정리에서 사용한다. |
| `page_get_type(struct page *page)` | 활용 함수 | `VM_UNINIT` 페이지까지 고려해서 실제 페이지 타입을 반환한다. 타입별 분기가 필요할 때 사용한다. |

## 2. 보조 페이지 테이블(SPT)

위치: `pintos/include/vm/vm.h`, `pintos/vm/vm.c`, `pintos/include/lib/kernel/hash.h`

| 이름 | 수정/활용 | 설명 |
| --- | --- | --- |
| `struct supplemental_page_table` | 수정 | 현재 비어 있다. Project 3에서는 보통 `struct hash`를 멤버로 넣어 가상 주소 기준으로 `struct page`를 관리한다. |
| `supplemental_page_table_init(struct supplemental_page_table *spt)` | 구현 | 프로세스별 SPT를 초기화한다. hash를 사용한다면 `hash_init()`을 호출하고, hash/less 콜백도 정의해야 한다. |
| `spt_find_page(struct supplemental_page_table *spt, void *va)` | 구현 | 임의의 주소 `va`를 페이지 경계로 내린 뒤 SPT에서 해당 page를 찾는다. page fault 처리와 중복 매핑 검증에서 핵심이다. |
| `spt_insert_page(struct supplemental_page_table *spt, struct page *page)` | 구현 | 새 page를 SPT에 삽입한다. 같은 `va`가 이미 있으면 실패해야 한다. |
| `spt_remove_page(struct supplemental_page_table *spt, struct page *page)` | 확인/수정 | page를 SPT에서 제거하고 `vm_dealloc_page()`까지 이어지게 해야 한다. 현재 skeleton은 SPT 자료구조 삭제가 빠져 있으므로 구현 방식에 맞게 보완해야 한다. |
| `supplemental_page_table_copy(dst, src)` | 구현 | `fork()`에서 부모의 SPT를 자식에게 복사한다. lazy page, loaded page, file-backed page 각각의 복사 정책을 정해야 한다. |
| `supplemental_page_table_kill(struct supplemental_page_table *spt)` | 구현 | 프로세스 종료 시 SPT 안의 모든 page를 순회하며 `destroy()`/writeback/free를 수행한다. |
| `hash_init`, `hash_find`, `hash_insert`, `hash_delete`, `hash_destroy` | 활용 | SPT를 hash로 구현할 때 사용하는 기본 API다. `struct page` 안에 `struct hash_elem` 멤버를 추가해야 한다. |
| `hash_entry(HASH_ELEM, STRUCT, MEMBER)` | 매크로 | `struct hash_elem *`에서 이를 포함한 `struct page *`로 되돌릴 때 사용한다. |
| `hash_bytes`, `hash_int` | 활용 | 페이지 가상 주소를 key로 hash 값을 만들 때 쓸 수 있다. 보통 `page->va` 또는 페이지 번호를 기준으로 한다. |

## 3. 페이지 할당, claim, page fault 처리

위치: `pintos/vm/vm.c`, `pintos/userprog/exception.c`

| 이름 | 수정/활용 | 설명 |
| --- | --- | --- |
| `vm_init(void)` | 일부 수정 | VM 서브시스템 전체 초기화 함수다. anon/file 초기화는 이미 호출한다. frame table, swap table 등 전역 자료구조를 만든다면 여기에서 초기화한다. |
| `vm_alloc_page(type, upage, writable)` | 매크로 | initializer 없이 page를 예약할 때 쓰는 wrapper다. 내부적으로 `vm_alloc_page_with_initializer()`를 호출한다. |
| `vm_alloc_page_with_initializer(type, upage, writable, init, aux)` | 구현 | lazy loading을 위해 아직 물리 프레임이 없는 page 객체를 만들고 SPT에 등록한다. `type`에 맞는 page initializer를 선택해 `uninit_new()`를 호출해야 한다. |
| `vm_claim_page(void *va)` | 구현 | SPT에서 `va`에 해당하는 page를 찾고 실제 frame을 붙인다. `setup_stack()`이나 page fault 처리에서 사용한다. |
| `vm_do_claim_page(struct page *page)` | 구현 | frame을 얻고 `pml4_set_page()`로 VA-KVA 매핑을 만든 뒤 `swap_in()`을 호출한다. Project 3의 실제 페이지 적재 경로다. |
| `vm_get_frame(void)` | 구현 | `palloc_get_page(PAL_USER)`로 사용자 풀에서 frame을 할당한다. 실패하면 `vm_evict_frame()`으로 victim을 축출해야 한다. frame table에 새 frame을 등록하는 책임도 가진다. |
| `vm_get_victim(void)` | 구현 | eviction 정책으로 victim frame을 고른다. clock 알고리즘을 쓰면 `pml4_is_accessed()`와 `pml4_set_accessed()`를 함께 활용한다. |
| `vm_evict_frame(void)` | 구현 | victim page를 `swap_out()`하고 frame을 재사용 가능한 상태로 반환한다. 기존 page와 frame의 연결 정리도 필요하다. |
| `vm_try_handle_fault(f, addr, user, write, not_present)` | 구현 | `page_fault()`에서 호출되는 Project 3 핵심 진입점이다. 유효한 lazy page면 claim하고, 스택 증가 조건이면 `vm_stack_growth()`를 수행한다. 권한 위반이면 실패해야 한다. |
| `vm_stack_growth(void *addr)` | 구현 | stack growth 조건을 만족할 때 새 stack page를 만들고 claim한다. 주소는 page boundary로 내린 뒤 매핑해야 한다. |
| `vm_handle_wp(struct page *page)` | 선택 구현 | write-protected page fault 처리용이다. 기본 Project 3에서는 false로 두거나 COW를 구현할 때 사용한다. |
| `vm_dealloc_page(struct page *page)` | 활용/수정 금지 | `destroy(page)` 후 `free(page)`를 호출한다. page 제거 시 직접 free하지 말고 이 경로를 쓰는 편이 안전하다. |
| `page_fault(struct intr_frame *f)` | 확인/연동 | `exception.c`의 page fault handler다. 현재 `#ifdef VM`에서 `vm_try_handle_fault()`를 호출하므로, 대부분의 정책은 `vm_try_handle_fault()` 안에 넣는다. |
| `PF_P`, `PF_W`, `PF_U` | 매크로 | page fault error code 해석용이다. `PF_P == 0`이면 not-present, `PF_W`는 write fault, `PF_U`는 user fault 여부다. |

## 4. Lazy loading과 uninit page

위치: `pintos/include/vm/uninit.h`, `pintos/vm/uninit.c`, `pintos/userprog/process.c`

| 이름 | 수정/활용 | 설명 |
| --- | --- | --- |
| `typedef bool vm_initializer(struct page *, void *aux)` | 활용 | lazy load 콜백 타입이다. `process.c`의 `lazy_load_segment()`가 이 타입으로 호출된다. |
| `struct uninit_page` | 구조체 | 아직 실제 타입으로 초기화되지 않은 page의 정보를 담는다. `init`, `type`, `aux`, `page_initializer`를 저장한다. |
| `uninit_new(page, va, init, type, aux, initializer)` | 활용/수정 금지 | `VM_UNINIT` page를 만든다. `vm_alloc_page_with_initializer()`에서 호출해야 한다. |
| `uninit_initialize(struct page *page, void *kva)` | 확인/필요 시 수정 | 첫 page fault에서 호출된다. 타입별 initializer를 먼저 호출하고, 이후 `lazy_load_segment()` 같은 init callback을 호출한다. |
| `uninit_destroy(struct page *page)` | 구현 | 한 번도 fault가 나지 않은 lazy page가 프로세스 종료 시 남을 수 있다. 이때 `aux`에 동적 할당한 정보가 있다면 여기서 해제해야 한다. |
| `lazy_load_segment(struct page *page, void *aux)` | 구현 | 실행 파일 segment를 실제로 파일에서 읽어 frame에 적재한다. `aux`에는 file, offset, read_bytes, zero_bytes 같은 정보가 필요하다. |
| `load_segment(file, ofs, upage, read_bytes, zero_bytes, writable)` | 수정 | Project 2처럼 즉시 page를 읽지 않고, 페이지별 aux를 만들고 `vm_alloc_page_with_initializer()`로 lazy page를 SPT에 등록해야 한다. |
| `setup_stack(struct intr_frame *if_)` | 수정 | 최초 stack page를 `VM_ANON` 또는 stack marker가 있는 page로 등록하고 즉시 `vm_claim_page()` 해야 한다. 성공하면 `if_->rsp = USER_STACK`으로 설정한다. |

## 5. Anonymous page와 swap

위치: `pintos/include/vm/anon.h`, `pintos/vm/anon.c`

| 이름 | 수정/활용 | 설명 |
| --- | --- | --- |
| `struct anon_page` | 수정 | anonymous page의 swap 위치, swap slot index, 상태 bit 등을 저장할 공간이다. |
| `vm_anon_init(void)` | 구현 | swap disk를 찾고 swap bitmap/table을 초기화한다. `disk_get(1, 1)` 같은 방식으로 swap disk를 얻는 구현이 일반적이다. |
| `anon_initializer(page, type, kva)` | 구현 | page의 operations를 `anon_ops`로 설정하고 anon 전용 필드를 초기화한다. |
| `anon_swap_in(page, kva)` | 구현 | swap slot에 저장된 page 내용을 `kva`로 읽어 온다. 처음 로드되는 anonymous page라면 0으로 채우는 정책이 필요하다. |
| `anon_swap_out(page)` | 구현 | victim anonymous page 내용을 swap disk의 빈 slot에 쓴다. swap slot이 부족하면 실패 처리해야 한다. |
| `anon_destroy(page)` | 구현 | anonymous page가 사용 중이던 swap slot이 있다면 반환한다. |

## 6. File-backed page와 mmap/munmap

위치: `pintos/include/vm/file.h`, `pintos/vm/file.c`, `pintos/userprog/syscall.c`

| 이름 | 수정/활용 | 설명 |
| --- | --- | --- |
| `struct file_page` | 수정 | file-backed page의 file pointer, offset, read_bytes, zero_bytes, writable 여부, mapping 정보 등을 저장한다. |
| `vm_file_init(void)` | 필요 시 구현 | file-backed VM 전역 초기화가 필요하면 사용한다. 기본 구현에서는 비어 있어도 된다. |
| `file_backed_initializer(page, type, kva)` | 구현 | page의 operations를 `file_ops`로 설정하고 file page 메타데이터를 초기화한다. |
| `file_backed_swap_in(page, kva)` | 구현 | file의 offset에서 `read_bytes`만큼 읽고 나머지 `zero_bytes`를 0으로 채운다. `file_read_at()`을 주로 사용한다. |
| `file_backed_swap_out(page)` | 구현 | dirty page라면 file에 다시 쓴다. `pml4_is_dirty()`로 dirty 여부를 보고 `file_write_at()`을 호출한다. |
| `file_backed_destroy(page)` | 구현 | mmap된 page가 제거될 때 필요한 writeback과 file close/reopen 정리를 수행한다. |
| `do_mmap(addr, length, writable, file, offset)` | 구현 | syscall `mmap`의 실제 구현이다. 주소 정렬, 길이, fd, offset, 기존 매핑 충돌을 검증하고 file-backed lazy page들을 SPT에 등록한다. 성공 시 시작 주소, 실패 시 `MAP_FAILED` 계열 값을 반환한다. |
| `do_munmap(void *addr)` | 구현 | `addr`로 시작하는 mmap 영역을 해제한다. 각 page의 dirty writeback, SPT 제거, frame/page table 정리가 필요하다. |
| `SYS_MMAP`, `SYS_MUNMAP` | syscall 번호 | `syscall_handler()`에 case를 추가해 `do_mmap()`/`do_munmap()`과 연결해야 한다. |
| `find_fd_entry(int fd)` | 활용/필요 시 조정 | syscall에서 fd를 `struct file *`로 바꿀 때 사용한다. `mmap`은 stdin/stdout fd를 거부해야 한다. |
| `file_reopen`, `file_read_at`, `file_write_at`, `file_length` | 활용 | mmap은 fd close 이후에도 매핑이 유지되어야 하므로 `file_reopen()`으로 독립 file 객체를 잡는 것이 안전하다. page 단위 I/O에는 `*_at` 함수가 적합하다. |

## 7. 프로세스 생명주기와 fork

위치: `pintos/userprog/process.c`, `pintos/include/threads/thread.h`

| 이름 | 수정/활용 | 설명 |
| --- | --- | --- |
| `struct thread.spt` | 구조체 필드 | 각 thread/process가 자기 SPT를 가진다. `thread.h`에 이미 `struct supplemental_page_table spt`가 있다. |
| `initd()` | 확인 | 첫 user process 시작 전 `supplemental_page_table_init()`을 호출한다. |
| `__do_fork(void *aux)` | 확인/수정 | `#ifdef VM`에서 자식 SPT를 초기화하고 `supplemental_page_table_copy()`를 호출한다. SPT copy 구현이 fork 동작의 핵심이다. |
| `process_cleanup(void)` | 확인/수정 | 현재 `supplemental_page_table_kill()`을 호출한다. SPT kill에서 모든 page/frame/file/swap 자원을 정리해야 한다. |
| `process_exit(void)` | 확인/필요 시 수정 | fd table과 실행 파일 정리 후 `process_cleanup()`으로 이어진다. mmap이 fd와 독립적으로 유지되어야 하므로 file lifetime을 잘 나누어야 한다. |

## 8. 주소, page table, frame 할당 보조 API

위치: `pintos/include/threads/vaddr.h`, `pintos/include/threads/mmu.h`, `pintos/include/threads/palloc.h`

| 이름 | 구분 | 설명 |
| --- | --- | --- |
| `PGSIZE` | 매크로 | 한 페이지 크기(4096 bytes)다. segment 분할, stack growth, mmap length 계산에 계속 사용한다. |
| `PGMASK`, `pg_ofs(va)` | 매크로 | 페이지 내부 offset을 구한다. 주소 정렬 검증에 사용한다. |
| `pg_round_down(va)` | 매크로 | 주소를 해당 page의 시작 주소로 내린다. SPT lookup key는 보통 이 값이다. |
| `pg_round_up(va)` | 매크로 | 주소 또는 길이를 page boundary로 올린다. mmap 범위 계산에 유용하다. |
| `USER_STACK` | 매크로 | user stack 최상단 주소다. 최초 stack page는 `USER_STACK - PGSIZE`에 만든다. |
| `is_user_vaddr(vaddr)` | 매크로 | 주소가 user 영역인지 검사한다. page fault, syscall pointer, mmap 주소 검증에 사용한다. |
| `is_kernel_vaddr(vaddr)` | 매크로 | 주소가 kernel 영역인지 검사한다. user mapping이 kernel 영역을 침범하지 않게 막는다. |
| `palloc_get_page(PAL_USER \| PAL_ZERO)` | 함수/flag | user pool에서 물리 page를 할당한다. frame을 만들 때는 반드시 `PAL_USER`를 사용해야 VM 테스트의 메모리 제한과 맞는다. |
| `palloc_free_page(page)` | 함수 | frame이 완전히 필요 없어졌을 때 물리 page를 반환한다. |
| `pml4_get_page(pml4, upage)` | 함수 | 현재 page table에 upage가 이미 매핑되어 있는지 확인한다. 중복 매핑 방지와 pointer 검증에 사용한다. |
| `pml4_set_page(pml4, upage, kpage, writable)` | 함수 | user VA를 kernel VA(frame)에 매핑한다. `vm_do_claim_page()`에서 반드시 호출해야 한다. |
| `pml4_clear_page(pml4, upage)` | 함수 | page table mapping을 제거한다. munmap, eviction, destroy 경로에서 필요하다. |
| `pml4_is_dirty`, `pml4_set_dirty` | 함수 | file-backed page writeback 여부 판단과 dirty bit 초기화에 사용한다. |
| `pml4_is_accessed`, `pml4_set_accessed` | 함수 | clock eviction 정책에서 최근 접근 여부를 확인하고 초기화할 때 사용한다. |

## 9. 구현 체크 포인트

- `struct page`에 SPT용 `hash_elem`을 추가할지 먼저 결정한다.
- `struct supplemental_page_table`에 hash table을 넣고 init/find/insert/remove/kill/copy를 먼저 완성한다.
- `load_segment()`는 즉시 파일을 읽지 말고 `lazy_load_segment()`에 필요한 aux를 page별로 만들어 SPT에 등록한다.
- `vm_try_handle_fault()`는 not-present fault, user/kernel 주소, write 권한, stack growth 조건을 명확히 나누어야 한다.
- `vm_do_claim_page()`는 `vm_get_frame() -> pml4_set_page() -> swap_in()` 순서로 구성한다.
- eviction을 구현하면 frame table 동기화와 page/frame 연결 해제를 반드시 같이 처리한다.
- `mmap`은 주소 정렬, 길이 0, fd 0/1, offset 정렬, 기존 SPT page와 겹침을 모두 거부해야 한다.
- `munmap`과 process exit은 dirty file-backed page를 writeback하고 SPT/page table/frame/file/swap 자원을 누수 없이 정리해야 한다.
