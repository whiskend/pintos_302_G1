# User Programs 구현 정리

> 기준 브랜치: `feat/userprog`  
> 비교 기준: `origin/main` merge-base `6f57a9f` -> 현재 HEAD `ad5f271`  
> 주요 구현 파일: `pintos/userprog/process.c`, `pintos/userprog/syscall.c`, `pintos/include/threads/thread.h`, `pintos/include/userprog/fd.h`, `pintos/userprog/exception.c`

## 섹션 1: 맥락

Pintos Project 2의 User Programs 구현은 커널이 단순히 테스트용 커널 스레드를 실행하는 단계를 넘어, 유저 프로세스를 로드하고 유저 코드가 커널 기능을 syscall로 요청할 수 있게 만드는 기반 작업이다.

초기 코드에서는 유저 프로그램 실행에 필요한 많은 기능이 TODO 상태였다. 대표적으로 커맨드라인 인자를 유저 스택에 올리는 argument passing, syscall dispatch, 파일 descriptor 관리, 부모-자식 프로세스의 wait/exit 동기화, fork 시 주소 공간과 fd 복사, 잘못된 유저 포인터 접근 처리 등이 구현되어 있지 않았다. 이 상태에서는 `args-*`, `halt`, `exit`, `read/write`, `open/close`, `exec`, `fork`, `wait` 계열 테스트를 안정적으로 통과할 수 없었다.

이번 구현으로 마련한 기반은 다음과 같다.

- 유저 프로그램이 `main(int argc, char *argv[])` 형태로 인자를 받을 수 있는 초기 유저 스택 구성
- `syscall` 명령으로 들어온 요청을 syscall 번호별로 분기하고 반환값을 `rax`에 담는 syscall 인터페이스
- 프로세스별 fd table과 `struct file *` 연결
- `read`, `write`, `open`, `close`, `filesize`, `seek`, `tell`, `dup2` 같은 파일 syscall의 기본 동작
- 잘못된 유저 포인터가 커널 panic으로 이어지지 않도록 하는 포인터 및 버퍼 검증
- 부모와 자식 사이의 exit status 전달, wait 1회 제한, wait/exit 동기화
- `fork`에서 부모의 trap frame, 주소 공간, fd table을 자식에게 복사하는 기반
- 실행 중인 executable에 대한 write deny 처리

해결한 문제는 크게 네 가지로 볼 수 있다.

1. 유저 프로그램 시작 문제  
   커맨드라인 전체를 실행 파일 이름으로 해석하던 문제를 줄이고, 프로그램 이름과 인자를 분리해 유저 스택에 배치했다. 이를 통해 `args-none`, `args-single`, `args-multiple`, `args-many`, `args-dbl-space` 같은 argument passing 테스트를 통과할 수 있는 기반을 만들었다.

2. syscall 처리 문제  
   기존 `syscall_handler()`는 실제 syscall 번호를 해석하지 못하고 종료하는 수준이었다. 지금은 syscall 번호를 기준으로 `halt`, `exit`, `fork`, `exec`, `wait`, 파일 syscall, `dup2` 등을 처리한다.

3. 자원 생명주기 문제  
   프로세스 종료 시 fd table, 실행 파일 write deny, 부모-자식 상태 구조체를 정리하도록 했다. 이를 통해 파일 누수, wait 상태 유실, 부모/자식 race condition 문제를 줄였다.

4. 커널 안정성 문제  
   유저가 넘긴 잘못된 주소를 커널이 그대로 역참조하면 page fault 또는 kernel panic이 발생할 수 있다. 포인터, 문자열, 버퍼 검증 헬퍼를 추가해 유저 프로세스만 `exit(-1)`로 종료되도록 했다.

이 기반 위에서 앞으로 Pintos에서 가능해지는 일은 다음과 같다.

- 유저 프로그램이 인자를 받아 실행된다.
- 유저 프로그램이 syscall을 통해 파일을 만들고 열고 읽고 쓸 수 있다.
- 부모 프로세스가 자식 프로세스를 만들고, 종료 상태를 기다릴 수 있다.
- `exec`를 통해 다른 유저 프로그램으로 교체 실행할 수 있다.
- `fork` 이후 부모/자식이 독립된 주소 공간과 fd 상태를 가지고 실행될 수 있다.
- fd 복사와 `dup2`를 기반으로 표준 입출력 재지정과 fd inheritance 계열 테스트를 처리할 수 있다.
- Project 3 VM에서 page fault, lazy loading, stack growth를 다룰 때 필요한 user/kernel 주소 구분 감각을 이어갈 수 있다.

## 섹션 2: 코드 수정한 부분

### `pintos/include/threads/thread.h`

초기 `struct thread`에는 userprog에서 필요한 프로세스별 상태가 거의 없었다. 이번 구현에서는 fd table, 자식 상태 목록, wait 상태, 실행 중인 파일 포인터를 추가했다.

관련 주석:

```c
/* A kernel thread or user process. */
```

추가된 구조:

```c
#ifdef USERPROG
struct child_status {
	tid_t tid;
	bool exited;
	bool waited;
	bool parent_alive;
	int exit_code;
	struct semaphore wait_sema;
	struct list_elem elem;
};
#endif
```

추가된 `struct thread` 필드:

```c
#ifdef USERPROG
	uint64_t *pml4;                     /* Page map level 4 */
	struct list fd_table;
	
	/* Variables for wait-exit(parent - child) synchronization. */
	struct list children;
	struct child_status *wait_status;
	struct file *running_file;
#endif
```

의미:

- `fd_table`: 현재 프로세스가 연 파일 descriptor 목록
- `children`: 부모가 기다릴 수 있는 자식 프로세스 상태 목록
- `wait_status`: 자식 입장에서 부모와 공유하는 종료 상태 기록지
- `running_file`: 현재 실행 중인 executable file object
- `child_status`: `wait()`와 `exit()` 사이에서 exit code와 동기화를 공유하는 구조체

### `pintos/include/userprog/fd.h`

새 파일을 추가해 fd table entry와 공유 fd 상태를 분리했다.

```c
struct fd_entry {
	int fd;
	struct shared_fd *sfd;
	struct list_elem file_elem;
};

struct shared_fd {
	int type;
	int shared_count;
	struct file *file;
};
```

의미:

- `fd_entry`: 한 프로세스의 fd table에 들어가는 fd 번호 단위 entry
- `shared_fd`: `dup2`나 fork fd 복사 이후 여러 fd entry가 같은 file 상태를 공유할 수 있도록 만든 구조
- `shared_count`: 같은 `shared_fd`를 참조하는 fd entry 수
- `type`: `STDIN_FILENO`, `STDOUT_FILENO`, `FILE_TYPE` 구분

### `pintos/userprog/process.c`

#### `process_init()`

원래는 현재 스레드를 가져오기만 하고 실질 초기화가 없었다.

관련 주석:

```c
/* General process initializer for initd and other process. */
```

변경 내용:

- 현재 프로세스의 fd table에 stdin, stdout entry를 등록한다.
- fd 0은 `STDIN_FILENO`, fd 1은 `STDOUT_FILENO` 타입으로 둔다.
- 일반 파일 fd가 2부터 시작할 수 있는 기반을 만든다.

#### `init_child_status()`

새로 만든 helper다.

역할:

- 부모의 `children` 리스트에 들어갈 `child_status`를 생성한다.
- `waited`, `exited`, `parent_alive`, `exit_code` 기본값을 설정한다.
- `wait_sema`를 0으로 초기화해 자식 종료 전까지 부모가 기다릴 수 있게 한다.

#### `process_create_initd()`

관련 주석:

```c
/* Starts the first userland program, called "initd", loaded from FILE_NAME. */
```

변경 내용:

- 전체 커맨드라인에서 첫 토큰만 추출해 thread name으로 사용한다.
- `initd_info` 구조체를 만들어 `f_name`과 `child_status`를 함께 자식에게 넘긴다.
- `thread_create()` 실패 시 `fn_copy`, `child_status`, `initd_info`를 정리한다.

기반이 된 문제:

- 스레드 이름이 `"args-single onearg"`처럼 인자까지 포함되면 종료 메시지와 테스트 출력이 어긋날 수 있다.
- 첫 유저 프로세스도 wait/exit 상태 구조체를 가져야 이후 종료 흐름을 통일할 수 있다.

#### `initd()`

관련 주석:

```c
/* A thread function that launches first user process. */
```

변경 내용:

- `initd_info`에서 커맨드라인과 `child_status`를 꺼낸다.
- 현재 스레드의 `wait_status`에 `child_status`를 연결한다.
- `process_init()` 후 `process_exec()`로 유저 프로그램을 시작한다.

#### `process_fork()`

관련 주석:

```c
/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
```

변경 내용:

- `fork_info`를 동적 할당해 부모 thread, 부모 trap frame 복사본, `child_status`를 자식에게 넘긴다.
- 부모의 `intr_frame`을 커널 스택 지역 변수 주소로 넘기지 않고 heap에 복사한다.
- 자식 생성 후 부모가 `sema_down(&cs->wait_sema)`로 자식의 fork 준비 완료를 기다린다.
- 자식 쪽 복사 실패 시 `TID_ERROR`를 반환할 수 있게 한다.

해결한 문제:

- `thread_create()` 이후 부모가 먼저 진행하면서 자식이 아직 복사하지 않은 부모 상태를 잃는 race condition
- fork 실패 시 부모에게 실패를 전달하지 못하는 문제

#### `duplicate_pte()`

기존 TODO를 구현했다.

관련 주석:

```c
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
```

변경 내용:

- kernel page table entry는 복사하지 않고 건너뛴다.
- 부모의 유저 페이지를 `pml4_get_page()`로 찾는다.
- 새 유저 페이지를 `palloc_get_page(PAL_USER | PAL_ZERO)`로 할당한다.
- 부모 페이지 내용을 `memcpy()`로 복사한다.
- 원래 writable 여부를 유지해 자식 pml4에 매핑한다.
- 실패 시 새 페이지를 해제하고 false를 반환한다.

#### `__do_fork()`

관련 주석:

```c
/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
```

변경 내용:

- `fork_info`에서 부모 thread, 부모 `intr_frame`, `child_status`를 받는다.
- 부모 trap frame을 자식 local `if_`로 복사하고 heap 복사본은 해제한다.
- 부모 주소 공간을 복제한다.
- 부모 fd table을 순회해 자식 fd table에 복사한다.
- 자식의 fork 반환값은 `if_.R.rax = 0`으로 설정한다.
- fork 준비가 끝나면 `sema_up(&cs->wait_sema)`로 부모를 깨운다.

#### `process_exec()`

관련 주석:

```c
/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
```

변경 내용:

- `f_name == NULL`이면 `-1`을 반환한다.
- 커맨드라인 첫 토큰을 실행 파일 이름으로 분리해 `load()`에 넘긴다.
- 인자 토큰을 `argv_tokens[32]`에 모은다.
- 각 인자 문자열을 유저 스택에 복사하고, 복사된 유저 주소를 `arg_addrs[32]`에 저장한다.
- 8바이트 정렬 후 `argv[argc] = NULL` sentinel을 쌓는다.
- `argv` 포인터 배열을 유저 스택에 구성한다.
- `if_.R.rdi = argc`, `if_.R.rsi = argv 주소`를 설정한다.
- 성공/실패 경로 모두에서 커맨드라인 페이지를 해제한다.

이 함수가 마련한 기반:

- `args-*` 테스트 통과
- `exec("prog arg1 arg2")` 구현의 기반
- 유저 스택과 커널 임시 버퍼 생명주기 분리

#### `process_wait()`

관련 주석:

```c
/* Waits for thread TID to die and returns its exit status. */
```

변경 내용:

- 현재 프로세스의 `children` 리스트에서 `child_tid`를 찾는다.
- 자식이 아니거나 이미 wait한 경우 `-1`을 반환한다.
- 아직 종료되지 않은 자식이면 `sema_down()`으로 기다린다.
- 자식 종료 후 exit code를 읽고 `child_status`를 리스트에서 제거한 뒤 해제한다.

해결한 문제:

- 부모가 자식 종료를 기다리지 못하던 문제
- 같은 자식을 두 번 wait하는 문제
- 종료 상태를 부모에게 전달하지 못하던 문제

#### `process_exit()`

관련 주석:

```c
/* Exit the process. This function is called by thread_exit (). */
```

변경 내용:

- fd table을 모두 순회하며 fd entry와 shared fd를 정리한다.
- 실행 중인 파일에 대해 `file_allow_write()` 후 `file_close()`를 수행한다.
- 현재 프로세스의 `wait_status`에 exit 상태를 기록한다.
- 종료 메시지 `<process>: exit(<status>)`를 출력한다.
- `sema_up()`으로 부모의 `process_wait()`를 깨운다.

#### `load()`

관련 주석:

```c
/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
```

변경 내용:

- 실행 파일 로드 성공 후 `thread_current()->running_file = file`로 저장한다.
- `file_deny_write(file)`로 실행 중인 파일에 대한 쓰기를 막는다.
- `file = NULL`로 넘겨 `done:`에서 닫히지 않도록 하고, `process_exit()`에서 닫는다.

해결한 문제:

- 실행 중인 executable에 쓰기를 허용하면 ROX 테스트에서 실패할 수 있다.
- load 성공 후 파일을 바로 닫으면 실행 파일 write deny 상태를 프로세스 생명주기와 연결할 수 없다.

### `pintos/userprog/syscall.c`

#### 포인터 검증 helper

추가된 helper:

```c
static bool is_valid_ptr (const void *ptr);
static bool is_valid_buffer (const void *buffer, int size);
static bool is_valid_string (const char *str);
static bool copy_user_string_to_page (const char *src, char *dst);
```

역할:

- `is_valid_ptr()`: `NULL`, kernel address, unmapped page를 거른다.
- `is_valid_buffer()`: read/write 버퍼가 걸치는 모든 page를 검사한다.
- `is_valid_string()`: 문자열 끝의 `'\0'`까지 유효한 유저 주소인지 확인한다.
- `copy_user_string_to_page()`: `exec()`용 커맨드라인을 안전하게 커널 페이지로 복사한다.

#### `sys_exit()`

역할:

- 현재 thread의 `wait_status`에 exit status를 저장한다.
- `thread_exit()`로 종료 흐름을 `process_exit()`에 넘긴다.

#### `syscall_handler()`

관련 주석:

```c
/* The main system call interface */
```

초기 코드:

```c
printf ("system call!\n");
thread_exit ();
```

현재 구현:

- `f->R.rax`에서 syscall 번호를 읽는다.
- `SYS_HALT`: `power_off()`
- `SYS_EXIT`: `sys_exit(status)`
- `SYS_FORK`: 유저 문자열 검증 후 `process_fork()`
- `SYS_EXEC`: 유저 문자열 검증 및 복사 후 `process_exec()`
- `SYS_WAIT`: `process_wait()`
- `SYS_CREATE`: `filesys_create()`
- `SYS_REMOVE`: `filesys_remove()`
- `SYS_OPEN`: `filesys_open()` 후 fd table 등록
- `SYS_FILESIZE`: fd 조회 후 `file_length()`
- `SYS_READ`: stdin, stdout, 일반 파일 fd 분기
- `SYS_WRITE`: stdout, 일반 파일 fd 분기
- `SYS_SEEK`: `file_seek()`
- `SYS_TELL`: `file_tell()`
- `SYS_CLOSE`: fd table에서 제거 후 자원 정리
- `SYS_DUP2`: fd entry가 같은 `shared_fd`를 공유하도록 연결
- default: 알 수 없는 syscall은 `sys_exit(-1)`

#### `find_fd_entry()`

역할:

- 현재 thread의 `fd_table`을 순회해 fd 번호에 맞는 `fd_entry`를 찾는다.
- 파일 syscall들이 공통으로 사용한다.

### `pintos/userprog/exception.c`

#### `page_fault()`

관련 주석:

```c
/* Page fault handler. */
```

변경 내용:

- user mode에서 발생한 page fault라면 현재 프로세스의 exit code를 `-1`로 설정하고 `thread_exit()`한다.
- kernel context fault는 기존처럼 fault 정보를 출력하고 `kill(f)`로 처리한다.

의미:

- 유저 프로그램의 잘못된 메모리 접근이 커널 전체 panic으로 번지는 것을 막는다.
- bad pointer 계열 테스트에서 프로세스만 비정상 종료되도록 하는 기반이다.

### 빌드 및 테스트 관련 파일

#### `pintos/userprog/Make.vars`

Project 2 extra test를 켰다.

```make
TDEFINE := -DEXTRA2
TEST_SUBDIRS += tests/userprog/dup2
GRADING_FILE = $(SRCDIR)/tests/userprog/Grading.extra
```

#### `pintos/userprog/Makefile`

`multi-oom` 단일 실행용 phony target을 추가했다.

```make
.PHONY: multi-oom
multi-oom:
	$(MAKE) check TESTS=tests/userprog/no-vm/multi-oom
```

#### `pintos/tests/userprog/Make.tests`, `pintos/tests/userprog/printtest.c`

stdout syscall 실험용 local smoke test를 추가했다.

```make
tests/userprog_PROGS += tests/userprog/printtest
tests/userprog/printtest_SRC = tests/userprog/printtest.c tests/lib.c
```

## 섹션 3: 구현하면서 어려웠던 부분 및 해결 과정

아래 항목들은 대화 중 실제로 질문했던 주제들이다. 아직 이 섹션에 확정 반영하지 않고, 어떤 항목을 "어려웠던 부분 및 해결 과정"으로 넣을지 분류 대기 상태로 둔다.

분류 후보:

1. argument parsing 구현 위치를 어디로 잡아야 하는지
2. 커맨드라인 원본, 버퍼, 원본 페이지의 차이
3. 유저 스택이 무엇이고 왜 `argv`를 유저 스택에 둬야 하는지
4. `if_->R.rdi`, `if_->R.rsi`, `if_->rsp` 접근에서 `.`과 `->` 차이
5. `args-none` 기준으로 구현 진척도를 어떻게 판단할지
6. `write(stdout)`와 `exit`이 뚫리지 않으면 `args-*`가 왜 통과하지 않는지
7. 실제 page fault가 난 것인지, 날 가능성이 있던 구조였는지
8. `cmd_page`를 `do_iret()` 전에 해제해도 유저 스택이 깨지지 않는 이유
9. `temp_arg`, `addr_arg` 배열 크기를 128, 64, 32 중 무엇으로 잡을지
10. `f_name == NULL` 실패 경로와 `load()` 실패 시 `palloc_free_page()` 누락 문제
11. fd table을 어떤 구조로 둘지
12. read/write 일반 fd 처리에서 invalid fd와 bad pointer를 어떻게 구분할지
13. boundary buffer를 시작 주소만 검사하면 안 되는 이유
14. process_wait에서 부모-자식 exit status를 언제까지 보존할지
15. fork에서 부모 trap frame을 어떻게 자식에게 안전하게 넘길지

질문:

- 위 후보 중 섹션 3에 넣을 항목 번호를 골라줘.
- 각 항목에 대해 "문제 상황 -> 원인 -> 해결" 형태로 길게 쓸지, 짧은 회고식으로 쓸지도 알려줘.
- 빠진 어려움이 있으면 항목을 추가해줘.

## 섹션 4: 처음 알게된 개념

아래 항목들도 대화 중 질문했던 주제들이다. 아직 이 섹션에 확정 반영하지 않고, 어떤 항목을 "처음 알게된 개념"으로 넣을지 분류 대기 상태로 둔다.

분류 후보:

1. 유저 스택과 커널 스택의 차이
2. 커널 스택이 4KB라 큰 지역 배열이 위험하다는 점
3. `strtok_r()`가 원본 문자열을 `'\0'`으로 바꾼다는 점
4. `cmdline_page`와 `file_name`을 구분해야 하는 이유
5. `argc`에는 `argv[0]`인 프로그램 이름도 포함된다는 점
6. `argv[argc] = NULL` sentinel
7. x86-64 syscall 인자가 `rax`, `rdi`, `rsi`, `rdx` 같은 레지스터로 전달된다는 점
8. syscall 반환값은 `f->R.rax`에 넣는다는 점
9. `pml4_get_page()`와 `is_user_vaddr()`로 유저 포인터를 검증하는 흐름
10. fd는 파일 자체가 아니라 프로세스별 열린 파일 table의 핸들이라는 점
11. `file_read()`와 `file_write()`가 file position을 이동시킨다는 점
12. `putbuf()`와 `input_getc()`의 역할
13. `sema_down()`과 `sema_up()`으로 wait/exit를 동기화하는 방식
14. `file_deny_write()`로 실행 중인 파일 쓰기를 막는 ROX 개념
15. `dup2`에서 두 fd가 같은 open file state를 공유해야 한다는 점

질문:

- 위 후보 중 섹션 4에 넣을 항목 번호를 골라줘.
- 개념 설명은 "정의 + Pintos에서 왜 필요한지 + 관련 코드" 형식으로 쓰면 될지 알려줘.
- 섹션 3과 겹치는 항목은 어느 쪽에 둘지 지정해줘.

