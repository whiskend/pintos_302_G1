# Argument Passing 헬퍼 함수 정리

이 문서는 Pintos Project 2의 argument passing을 구현할 때 자주 쓰는 함수와 매크로를 정리한 것이다.
목표는 커맨드라인 문자열을 파싱하고, `argc`, `argv`, 인자 문자열을 유저 스택에 올바르게 배치하는 것이다.

## 구현 위치

주로 볼 파일은 다음과 같다.

- `pintos/userprog/process.c`
- `pintos/include/threads/vaddr.h`
- `pintos/include/threads/interrupt.h`
- `pintos/include/lib/string.h`
- `pintos/lib/string.c`

`process.c`에서는 보통 `process_exec()`, `load()`, `setup_stack()` 주변을 수정하게 된다.

## 문자열 파싱

### `strtok_r()`

```c
char *strtok_r (char *s, const char *delimiters, char **save_ptr);
```

커맨드라인 문자열을 공백 기준으로 자를 때 쓴다.

예:

```c
char *save_ptr;
char *token = strtok_r (cmdline, " ", &save_ptr);
```

처음 호출할 때는 자를 문자열을 넣고, 그 다음부터는 `NULL`을 넣는다.

```c
for (token = strtok_r (cmdline, " ", &save_ptr);
     token != NULL;
     token = strtok_r (NULL, " ", &save_ptr)) {
  ...
}
```

주의할 점:

- `strtok_r()`는 원본 문자열 안의 공백을 `'\0'`으로 바꾼다.
- 그래서 읽기 전용 문자열이나 보존해야 하는 문자열에 직접 쓰면 안 된다.
- 여러 개의 연속 공백은 하나의 구분자로 처리한다.
- `args-dbl-space` 테스트에서 이 성질이 중요하다.

### `strlcpy()`

```c
size_t strlcpy (char *dst, const char *src, size_t size);
```

커맨드라인 원본을 별도 버퍼에 복사할 때 쓴다.

예:

```c
char *cmd_copy = palloc_get_page (0);
strlcpy (cmd_copy, file_name, PGSIZE);
```

주의할 점:

- `size`만큼 안전하게 복사하고 마지막에 `'\0'`을 붙여준다.
- Pintos에서는 `strcpy` 대신 `strlcpy`를 쓰는 흐름이 좋다.
- `strtok_r()`로 자를 문자열은 수정 가능한 버퍼여야 하므로, 보통 `strlcpy()`로 복사본을 만든 뒤 파싱한다.

### `strlen()`

```c
size_t strlen (const char *s);
```

인자 문자열을 유저 스택에 복사할 때 문자열 길이를 계산한다.

예:

```c
size_t len = strlen (argv[i]) + 1;
```

`+ 1`은 문자열 끝의 `'\0'`까지 같이 복사하기 위해 필요하다.

## 메모리 복사

### `memcpy()`

```c
void *memcpy (void *dst, const void *src, size_t size);
```

인자 문자열이나 포인터 값을 스택에 직접 복사할 때 쓴다.

예:

```c
if_->rsp -= len;
memcpy ((void *) if_->rsp, argv[i], len);
```

주의할 점:

- 문자열은 반드시 유저 스택 안으로 복사해야 한다.
- `char *argv[10]` 같은 지역 배열은 커널 스택에 있으므로, 그 주소를 그대로 유저 프로그램에 넘기면 안 된다.

### `memset()`

```c
void *memset (void *dst, int value, size_t size);
```

정렬용 패딩을 0으로 채우거나, 메모리를 초기화할 때 쓴다.

예:

```c
if_->rsp -= padding;
memset ((void *) if_->rsp, 0, padding);
```

## 페이지 할당과 해제

### `palloc_get_page()`

```c
void *palloc_get_page (enum palloc_flags flags);
```

4KB 페이지 하나를 할당한다.

자주 쓰는 플래그:

- `0`: 일반 커널 페이지
- `PAL_ZERO`: 0으로 초기화된 페이지
- `PAL_USER`: 유저 풀에서 페이지 할당

예:

```c
char *cmd_copy = palloc_get_page (0);
```

argument parsing에서는 커맨드라인 복사본을 만들 때 쓸 수 있다.

### `palloc_free_page()`

```c
void palloc_free_page (void *page);
```

`palloc_get_page()`로 받은 페이지를 해제한다.

예:

```c
palloc_free_page (cmd_copy);
```

주의할 점:

- 해제한 페이지 안의 문자열 주소를 유저 프로그램에 넘기면 안 된다.
- 그래서 인자 문자열은 해제될 임시 버퍼가 아니라 유저 스택 안에 다시 복사해야 한다.

## 유저 스택 관련

### `setup_stack()`

```c
static bool setup_stack (struct intr_frame *if_);
```

`process.c` 안에 있는 함수다.
유저 스택으로 쓸 4KB 페이지를 만들고, 성공하면 보통 다음처럼 설정한다.

```c
if_->rsp = USER_STACK;
```

즉, `setup_stack()`이 끝난 직후 `if_->rsp`는 유저 스택의 맨 위 주소를 가리킨다.
argument passing은 이 주소를 아래로 내리면서 문자열과 포인터들을 쌓는 작업이다.

### `install_page()`

```c
static bool install_page (void *upage, void *kpage, bool writable);
```

커널이 할당한 실제 페이지 `kpage`를 유저 가상 주소 `upage`에 매핑한다.

`setup_stack()` 안에서 보통 다음처럼 사용된다.

```c
install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
```

직접 argument parsing을 구현할 때 자주 호출하진 않지만, `setup_stack()`이 무슨 일을 하는지 이해하려면 알아야 한다.

### `USER_STACK`

```c
#define USER_STACK 0x47480000
```

유저 스택의 시작점, 더 정확히는 스택의 맨 위 주소다.
스택은 높은 주소에서 낮은 주소 방향으로 자라므로, 데이터를 넣을 때마다 `if_->rsp`를 감소시킨다.

예:

```c
if_->rsp -= len;
memcpy ((void *) if_->rsp, arg, len);
```

### `PGSIZE`

```c
#define PGSIZE (1 << PGBITS)
```

페이지 크기다. Pintos에서는 보통 4096바이트다.

스택 페이지 범위는 다음과 같이 생각하면 된다.

```text
USER_STACK - PGSIZE  <=  유저 스택 데이터  <  USER_STACK
```

## 레지스터 설정

### `struct intr_frame`

`load()`는 다음 형태로 `intr_frame` 포인터를 받는다.

```c
static bool load (const char *file_name, struct intr_frame *if_);
```

따라서 `if_`는 포인터다. 접근할 때는 `.`이 아니라 `->`를 쓴다.

```c
if_->R.rdi = argc;
if_->R.rsi = argv_addr;
if_->rsp = final_rsp;
```

지역 변수인 `_if` 자체에 접근할 때만 `.`을 쓴다.

```c
struct intr_frame _if;
_if.rsp = USER_STACK;
```

### `if_->R.rdi`

첫 번째 함수 인자 레지스터다.
유저 프로그램의 `main(int argc, char *argv[])`에서 `argc`가 여기에 들어간다고 보면 된다.

```c
if_->R.rdi = argc;
```

### `if_->R.rsi`

두 번째 함수 인자 레지스터다.
`argv` 배열의 유저 가상 주소를 넣는다.

```c
if_->R.rsi = (uint64_t) argv_addr;
```

주의할 점:

- `argv_addr`는 유저 스택 안의 주소여야 한다.
- 커널 지역 배열 `char *argv[10]`의 주소를 넣으면 안 된다.

### `if_->rsp`

유저 프로그램이 시작할 때 사용할 스택 포인터다.
문자열, 정렬 패딩, `argv` 포인터 배열 등을 모두 쌓은 뒤 최종 주소로 설정해야 한다.

```c
if_->rsp = final_rsp;
```

## 정렬에 쓰는 계산

### 8바이트 정렬

`args.c`는 `argv` 주소가 8바이트 정렬인지 확인한다.

```c
if (((unsigned long long) argv & 7) != 0)
  msg ("argv and stack must be word-aligned, actually %p", argv);
```

보통 스택에 문자열을 복사한 뒤 `if_->rsp`를 8의 배수로 맞춘다.

예:

```c
size_t padding = if_->rsp % 8;
if_->rsp -= padding;
memset ((void *) if_->rsp, 0, padding);
```

또는 비트 연산으로 내림 정렬할 수 있다.

```c
if_->rsp &= ~0x7;
```

## 디버깅용

### `hex_dump()`

```c
void hex_dump (uintptr_t ofs, const void *buf, size_t size, bool ascii);
```

스택에 데이터가 어떻게 들어갔는지 확인할 때 유용하다.

예:

```c
hex_dump (if_->rsp, (void *) if_->rsp, USER_STACK - if_->rsp, true);
```

주의할 점:

- 테스트 제출 전에는 디버그 출력은 제거해야 한다.
- `args-*` 테스트는 기대 출력이 정해져 있어서 불필요한 `printf()`가 있으면 실패할 수 있다.

## 추천 구현 흐름

1. 커맨드라인 문자열을 `palloc_get_page()`와 `strlcpy()`로 복사한다.
2. 복사본 하나는 실행 파일 이름 분리용으로 쓰고, 필요하면 다른 복사본 하나는 전체 인자 파싱용으로 쓴다.
3. `strtok_r()`로 토큰을 나누고 `argc`를 센다.
4. `setup_stack(if_)`으로 유저 스택 페이지를 만든다.
5. 마지막 인자부터 문자열을 유저 스택에 복사하고, 각 문자열의 유저 주소를 저장한다.
6. 스택 포인터를 8바이트 기준으로 정렬한다.
7. `argv[argc] = NULL`을 스택에 넣는다.
8. `argv[argc - 1]`부터 `argv[0]`까지 포인터를 스택에 넣는다.
9. 유저 스택 안의 `argv[0]` 주소를 `if_->R.rsi`에 넣는다.
10. `argc`를 `if_->R.rdi`에 넣는다.
11. 최종 스택 포인터를 `if_->rsp`에 남긴다.
12. 임시로 할당한 커맨드라인 복사본 페이지를 해제한다.

## 최소 체크리스트

- `filesys_open()`에는 전체 커맨드라인이 아니라 실행 파일 이름만 들어간다.
- `argv[0]`은 실행 파일 이름이다.
- `argc`는 실행 파일 이름까지 포함한다.
- `argv[argc]`는 `NULL`이다.
- `if_->R.rsi`는 커널 주소가 아니라 유저 스택 안의 `argv` 주소다.
- `if_->rsp`와 `argv` 주소는 8바이트 정렬 조건을 만족한다.
- 테스트용 `printf()`는 제거한다.
