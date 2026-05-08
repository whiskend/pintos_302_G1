# Read/Write FD 구현에 필요한 기존 함수와 매크로 정리

이 문서는 Pintos Project 2에서 fd table, `open()`, `remove()`, `filesize()`, `read()`, `write()`, `close()`를 구현할 때 가져다 쓸 수 있는 **이미 존재하는 함수와 매크로**를 정리한 것이다.

Argument passing에서 `strtok_r()`, `strlcpy()`, `memcpy()`를 가져다 썼던 것처럼, 여기서는 Pintos가 이미 제공하는 list, file, input/output, memory, address-check API를 확인한다.

주의할 점:

- Pintos에는 fd table을 완성해주는 전용 함수가 없다.
- fd table 자체는 직접 만들어야 한다.
- 아래 함수들은 fd table을 만들 때 쓰는 재료다.
- 이 문서에는 없는 custom helper 함수 이름을 쓰지 않는다.

## 구현 위치

주로 볼 파일은 다음과 같다.

- `pintos/userprog/syscall.c`
- `pintos/include/threads/thread.h`
- `pintos/threads/thread.c`
- `pintos/userprog/process.c`
- `pintos/include/userprog/syscall.h`
- `pintos/include/lib/kernel/list.h`
- `pintos/include/filesys/filesys.h`
- `pintos/include/filesys/file.h`
- `pintos/include/devices/input.h`
- `pintos/include/lib/kernel/stdio.h`
- `pintos/include/threads/malloc.h`
- `pintos/include/threads/vaddr.h`
- `pintos/include/threads/mmu.h`

## fd table list 관리

현재 코드에는 `struct thread` 안에 `struct list fd_table;`이 들어가 있다.
따라서 fd table을 다룰 때는 Pintos list API를 사용하면 된다.

### `list_init()`

```c
void list_init (struct list *);
```

`struct list`를 빈 list로 초기화한다.

예:

```c
list_init (&t->fd_table);
```

주의할 점:

- `init_thread()`에서 새 thread의 fd table을 초기화할 때 필요하다.
- 현재 코드의 B5 TODO 자리에서는 `donators`가 아니라 `fd_table`을 초기화해야 한다.

### `list_push_back()`

```c
void list_push_back (struct list *list, struct list_elem *elem);
```

list 뒤에 새 element를 붙인다.

예:

```c
list_push_back (&thread_current ()->fd_table, &entry->file_elem);
```

주의할 점:

- fd entry 구조체 안에는 `struct list_elem` 필드가 있어야 한다.
- 현재 코드에서는 `file_elem`이라는 이름을 쓰기 시작했다.

### `list_begin()`, `list_end()`, `list_next()`

```c
struct list_elem *list_begin (struct list *);
struct list_elem *list_end (struct list *);
struct list_elem *list_next (struct list_elem *);
```

list를 앞에서부터 순회할 때 쓴다.

예:

```c
for (struct list_elem *e = list_begin (&thread_current ()->fd_table);
     e != list_end (&thread_current ()->fd_table);
     e = list_next (e)) {
  ...
}
```

필요한 상황:

- fd 번호로 fd entry를 찾을 때
- 사용 중인 fd 번호를 확인할 때
- 종료 시 열려 있는 fd entry들을 정리할 때

### `list_entry`

```c
#define list_entry(LIST_ELEM, STRUCT, MEMBER)
```

`struct list_elem *`에서 그것을 포함한 실제 구조체 포인터를 얻는다.

예:

```c
struct fd_entry *entry = list_entry (e, struct fd_entry, file_elem);
```

주의할 점:

- 세 번째 인자는 fd entry 안의 `struct list_elem` 필드 이름과 같아야 한다.
- `file_elem`으로 만들었으면 `file_elem`을 넣고, `elem`으로 만들었으면 `elem`을 넣는다.

### `list_remove()`

```c
struct list_elem *list_remove (struct list_elem *);
```

list에서 element를 제거한다.

예:

```c
list_remove (&entry->file_elem);
```

필요한 상황:

- `close(fd)`에서 fd table entry를 제거할 때
- process exit cleanup에서 열린 fd들을 제거할 때

주의할 점:

- 순회하면서 제거할 때는 다음 element를 잃지 않도록 조심해야 한다.
- `list_remove()`는 제거된 다음 element를 반환한다.

### `list_empty()`

```c
bool list_empty (struct list *);
```

list가 비어 있는지 확인한다.

예:

```c
while (!list_empty (&thread_current ()->fd_table)) {
  ...
}
```

필요한 상황:

- process exit cleanup에서 남은 fd entry가 있는지 볼 때

## 현재 thread 접근

### `thread_current()`

```c
struct thread *thread_current (void);
```

현재 실행 중인 thread를 반환한다.

예:

```c
struct thread *cur = thread_current ();
```

필요한 상황:

- 현재 프로세스의 fd table에 접근할 때
- 현재 프로세스의 page table인 `pml4`에 접근할 때

주의할 점:

- Pintos Project 2에서는 유저 프로세스의 커널 쪽 상태가 사실상 `struct thread`에 있다.
- fd table은 전역이 아니라 현재 thread/process 기준으로 찾아야 한다.

## fd entry 메모리

### `malloc()`

```c
void *malloc (size_t size);
```

커널 heap에서 메모리를 할당한다.

예:

```c
struct fd_entry *entry = malloc (sizeof *entry);
```

필요한 상황:

- 파일을 열고 새 fd entry를 만들 때

주의할 점:

- 실패하면 `NULL`을 반환한다.
- 파일 열기는 성공했는데 fd entry 할당이 실패하면 열린 파일을 닫아야 한다.

### `free()`

```c
void free (void *);
```

`malloc()`으로 받은 메모리를 해제한다.

예:

```c
free (entry);
```

필요한 상황:

- `close(fd)`에서 fd entry를 제거한 뒤
- process exit cleanup에서 fd entry들을 정리할 때

## 파일 이름 기반 작업

### `filesys_remove()`

```c
bool filesys_remove (const char *name);
```

파일 이름으로 파일을 삭제한다.

예:

```c
bool ok = filesys_remove (file_name);
```

반환:

- 성공하면 `true`
- 실패하면 `false`

필요한 상황:

- `SYS_REMOVE`

주의할 점:

- 이 함수는 fd 번호를 받지 않는다.
- 인자는 열린 파일 객체가 아니라 파일 이름 문자열이다.
- fd table에서 entry를 찾는 작업과 관련이 없다.
- 이미 열려 있는 fd를 닫아주는 함수가 아니다.
- syscall 반환값은 `filesys_remove()`의 반환값을 직접 `f->R.rax`에 넣어야 한다.

### `filesys_open()`

```c
struct file *filesys_open (const char *name);
```

파일 이름으로 파일을 연다.

예:

```c
struct file *file = filesys_open (file_name);
```

반환:

- 성공하면 `struct file *`
- 실패하면 `NULL`

필요한 상황:

- `SYS_OPEN`

주의할 점:

- 이 함수는 fd 번호를 만들어주지 않는다.
- fd 번호 배정과 fd table 등록은 직접 구현해야 한다.

## 열린 파일 다루기

### `file_read()`

```c
off_t file_read (struct file *file, void *buffer, off_t size);
```

열린 파일의 현재 위치에서 데이터를 읽는다.

예:

```c
off_t bytes = file_read (file, buffer, size);
```

필요한 상황:

- fd 2 이상의 일반 파일에 대한 `SYS_READ`

주의할 점:

- 이 함수는 fd 번호를 받지 않는다.
- fd table에서 `struct file *`을 찾은 뒤 호출해야 한다.

### `file_write()`

```c
off_t file_write (struct file *file, const void *buffer, off_t size);
```

열린 파일의 현재 위치에 데이터를 쓴다.

예:

```c
off_t bytes = file_write (file, buffer, size);
```

필요한 상황:

- fd 2 이상의 일반 파일에 대한 `SYS_WRITE`

주의할 점:

- fd 1(stdout)은 `file_write()`가 아니라 `putbuf()`로 처리한다.
- 이 함수도 fd 번호가 아니라 `struct file *`을 받는다.

### `file_length()`

```c
off_t file_length (struct file *file);
```

열린 파일의 크기를 반환한다.

예:

```c
off_t len = file_length (file);
```

필요한 상황:

- `SYS_FILESIZE`

주의할 점:

- fd table에서 `struct file *`을 찾은 뒤 호출한다.

### `file_close()`

```c
void file_close (struct file *file);
```

열린 파일을 닫는다.

예:

```c
file_close (file);
```

필요한 상황:

- `SYS_CLOSE`
- process exit cleanup
- fd entry 생성 실패 시 이미 열린 파일을 정리할 때

주의할 점:

- 같은 `struct file *`을 두 번 닫으면 안 된다.
- fd table에서 제거하는 흐름과 함께 생각해야 한다.

## 표준 입출력

### `STDIN_FILENO`, `STDOUT_FILENO`

```c
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
```

표준 입력과 표준 출력 fd 번호다.

필요한 상황:

- `SYS_READ`
- `SYS_WRITE`

주의할 점:

- fd 0과 fd 1은 일반 파일 fd table lookup과 분리해서 생각하는 편이 단순하다.

### `putbuf()`

```c
void putbuf (const char *buffer, size_t n);
```

문자열 buffer를 콘솔에 출력한다.

예:

```c
putbuf (buffer, size);
```

필요한 상황:

- `write(1, buffer, size)`

주의할 점:

- `putbuf()`는 반환값을 설정해주지 않는다.
- syscall 반환값은 직접 `f->R.rax`에 넣어야 한다.

### `input_getc()`

```c
uint8_t input_getc (void);
```

입력 장치에서 문자 하나를 읽는다.

예:

```c
uint8_t ch = input_getc ();
```

필요한 상황:

- `read(0, buffer, size)`

주의할 점:

- 한 번에 한 byte씩 읽는다.
- 읽은 byte를 유저 buffer에 써야 하므로 buffer 검증과 연결된다.

## 유저 주소 검증

### `is_user_vaddr()`

```c
bool is_user_vaddr (const void *vaddr);
```

주소가 유저 가상 주소 범위인지 확인한다.

예:

```c
if (!is_user_vaddr (addr)) {
  ...
}
```

필요한 상황:

- syscall 인자로 받은 포인터 검증
- read/write buffer 검증
- open filename 문자열 검증

주의할 점:

- 이것만으로는 충분하지 않다.
- 유저 주소 범위여도 실제 page table에 매핑되지 않았을 수 있다.

### `pml4_get_page()`

```c
void *pml4_get_page (uint64_t *pml4, const void *upage);
```

유저 가상 주소가 현재 프로세스의 page table에 매핑되어 있는지 확인한다.

예:

```c
void *kaddr = pml4_get_page (thread_current ()->pml4, addr);
```

필요한 상황:

- syscall 포인터 검증
- read/write buffer 검증
- open filename 문자열 검증

주의할 점:

- 호출 전에 `is_user_vaddr()`로 유저 주소인지 먼저 확인해야 한다.
- 현재 프로세스의 page table은 `thread_current()->pml4`다.

### `pg_round_down()`

```c
#define pg_round_down(va)
```

주소를 그 주소가 속한 page의 시작 주소로 내린다.

예:

```c
void *page = pg_round_down (addr);
```

필요한 상황:

- buffer가 여러 page에 걸칠 때 page 단위로 검사할 때

주의할 점:

- `read-boundary`, `write-boundary`는 시작 주소 하나만 검사하는 구현을 잡아낸다.

### `PGSIZE`

```c
#define PGSIZE
```

page 크기다.

필요한 상황:

- page 단위로 주소를 증가시키며 buffer 범위를 검사할 때

## syscall 인자와 반환값

### `f->R.rax`

syscall 진입 시에는 syscall 번호가 들어 있고, syscall 처리 후에는 반환값을 넣는 레지스터다.

예:

```c
int syscall_num = f->R.rax;
f->R.rax = ret;
```

주의할 점:

- `write()`에서 `putbuf()`를 호출해도 반환값은 자동으로 설정되지 않는다.
- read/write/filesize/open/remove의 결과는 직접 `f->R.rax`에 넣어야 한다.

### `f->R.rdi`, `f->R.rsi`, `f->R.rdx`

64-bit Pintos에서 syscall의 앞쪽 인자가 들어오는 레지스터다.

예:

```c
int fd = (int) f->R.rdi;
void *buffer = (void *) f->R.rsi;
unsigned size = (unsigned) f->R.rdx;
```

필요한 상황:

- `open(file)`
- `remove(file)`
- `filesize(fd)`
- `read(fd, buffer, size)`
- `write(fd, buffer, size)`
- `close(fd)`

## 우선 볼 기존 함수 요약

- `list_init()`
- `list_push_back()`
- `list_begin()`
- `list_end()`
- `list_next()`
- `list_entry`
- `list_remove()`
- `list_empty()`
- `thread_current()`
- `malloc()`
- `free()`
- `filesys_remove()`
- `filesys_open()`
- `file_read()`
- `file_write()`
- `file_length()`
- `file_close()`
- `STDIN_FILENO`
- `STDOUT_FILENO`
- `putbuf()`
- `input_getc()`
- `is_user_vaddr()`
- `pml4_get_page()`
- `pg_round_down()`
- `PGSIZE`
