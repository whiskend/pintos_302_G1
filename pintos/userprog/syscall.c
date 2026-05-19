#include "userprog/syscall.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "userprog/fd.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "vm/vm.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
static struct fd_entry *find_fd_entry (int fd);
static bool is_valid_ptr (const void *ptr);
static bool is_valid_buffer (const void *buffer, int size, bool writable);
static bool is_valid_string (const char *str);
static bool copy_user_string_to_page (const char *src, char *dst);
static void sys_exit (int status);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

static bool
is_valid_ptr (const void *ptr) {
	if (ptr == NULL || !is_user_vaddr (ptr))
		return false;
	// page가 없는데?
	//return pml4_get_page (thread_current ()->pml4, ptr) != NULL;
	return spt_find_page(&thread_current()->spt, ptr);
}

// vm-write-code2 test에서 writable 을 check 하지 않음.
// 그래서 read-only 인 코드 영역에 write 하려고 해서 실패함.
// 로직 수정 필요해서 인자 'bool writable' 추가..
static bool
is_valid_buffer (const void *buffer, int size, bool writable) {
	// size가 음수면 안 됨.
	if (size < 0)
		return false;
	// size가 0이면 실제로 접근할 메모리가 없어서 valid.
	if (size == 0)
		return true;
	// buffer가 NULL이면 안 됨.
	if (buffer == NULL)
		return false;

	uint64_t start = (uint64_t) buffer;
	uint64_t end = start + size - 1;
	if (end < start)
		return false;

	// buffer가 걸쳐 있는 모든 페이지 검사.
	for (uint64_t page = (uint64_t) pg_round_down ((void *) start); page <= end; page += PGSIZE) {
		// is_valid_ptr()로 감싸면 안 됨.
		// 코드 페이지처럼 SPT에는 존재하지만 writable == false인 페이지는
		// is_valid_ptr()가 true라서 아래 writable 검사를 건너뛰게 된다.
		// read()의 목적지 버퍼는 반드시 writable이어야 하므로,
		// spt_find_page()로 page 구조체를 직접 얻은 뒤 항상 writable 여부를 검사해야 한다.
		struct page *p = spt_find_page (&thread_current()->spt, (void *) page);
			
		// spt에 page가 없는데? -> 없을 수 있지.
		// stack_growth 조건에 맞는 주소라면 stack page 생성 가능하니깐..
		if (p == NULL) {
			void *rsp = (void *) thread_current ()->user_rsp;
			// 디버깅용
			// printf ("buffer page missing: page=%p buffer=%p size=%d user_rsp=%p\n",
			
			// 로직 수정
			// 1. 유저 주소가 아니면 stack growth 조건이 아니니깐 false로 바로 리턴.
			if (!is_user_vaddr ((void *) page)) {
				return false;
			}
			// 2. stack growth가 가능한 주소라면, stack page 생성 or page fault 처리 허용
			// 일단 stack growth로 인정할 수 있는 주소인지 확인.
			// a. USER_STACK 아래여야 함.
			// b. 최대 stack 크기 안이어야 함.
			// c. 현재 user rsp 근처여야 함.
			if ((void *) page < USER_STACK &&
				(void *) page >= USER_STACK - STACK_MAX &&
				(void *) page >= rsp - 8) {
				// stack page는 파일에서 온 페이지가 아니므로 anonymous page.
				// page는 이미 pg_round_down 된 주소라 그대로 넣어도 됨.
				if (!vm_alloc_page (VM_ANON, (void *) page, true))
					return false;

				// syscall 안에서 바로 접근할 버퍼라 실제 frame까지 claim.
				if (!vm_claim_page ((void *) page))
					return false;

				// 생성 후 다시 SPT에서 page를 찾아서 아래 writable 검사를 이어감.
				p = spt_find_page (&thread_current ()->spt, (void *) page);
				if (p == NULL)
					return false;
			} else {
				// stack growth 조건도 안 맞으면 진짜 invalid 주소.
				return false;
			}
		}
		// read()의 목적지 buffer처럼 커널이 유저 buffer에 써야 하는 경우,
		// 해당 page가 writable이어야 함.
		// 이 검사가 없으면 pt-write-code2에서 코드 영역에 write하는 걸 못 막음.
		if (writable && !p->writable)
			return false;
	}

	// buffer가 걸친 모든 page가 조건을 통과했으면 valid.
	return true;
}

static bool
is_valid_string (const char *str) {
	for (;;) {
		if (!is_valid_ptr (str))
			return false;
		if (*str == '\0')
			return true;
		str++;
	}
}

static bool
copy_user_string_to_page (const char *src, char *dst) {
	for (size_t i = 0; i < PGSIZE; i++) {
		if (!is_valid_ptr (src + i))
			return false;
		dst[i] = src[i];
		if (dst[i] == '\0')
			return true;
	}
	return false;
}

static void
sys_exit (int status) {
	struct child_status *cs = thread_current ()->wait_status;
	if (cs != NULL)
		cs->exit_code = status;
	thread_exit ();
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	int syscall_num = f->R.rax;
	thread_current()->user_rsp = f->rsp;

	switch (syscall_num)
	{
		case SYS_HALT:
		{
			power_off ();
			break;
		}
		case SYS_FORK:
		{
			char *thread_name = (char *) f->R.rdi;
			if (!is_valid_string (thread_name))
				sys_exit (-1);
			f->R.rax = (tid_t) process_fork (thread_name, f);
			break;
		}
		case SYS_EXEC:
		{
			char *cmd_line = (char *) f->R.rdi;
			if (!is_valid_string (cmd_line))
				sys_exit (-1);
			char *cmd_line_copy = palloc_get_page (0);
			if (cmd_line_copy == NULL)
				sys_exit (-1);
			if (!copy_user_string_to_page (cmd_line, cmd_line_copy)) {
				palloc_free_page (cmd_line_copy);
				sys_exit (-1);
			}
			if (process_exec (cmd_line_copy) == -1)
				sys_exit (-1);
			break;
		}
		case SYS_WAIT:
		{
			tid_t tid = (tid_t) f->R.rdi;
			f->R.rax = process_wait (tid);
			break;
		}
		case SYS_EXIT:
		{
			sys_exit ((int) f->R.rdi);
			break;
		}
		case SYS_CREATE:
		{
			char *file_name = (char *) f->R.rdi;
			unsigned initial_size = f->R.rsi;
			if (!is_valid_string (file_name))
				sys_exit (-1);
			f->R.rax = filesys_create (file_name, initial_size);
			break;
		}
		case SYS_REMOVE:
		{
			char *file_name = (char *) f->R.rdi;
			if (!is_valid_string (file_name))
				sys_exit (-1);

			f->R.rax = filesys_remove(file_name);
			break;
		}
		case SYS_OPEN:
		{
			struct thread *cur = thread_current ();
			char *file_name = (char *) f->R.rdi;
			if (!is_valid_string (file_name))
				sys_exit (-1);

			struct file *file = filesys_open (file_name);
			if (file == NULL) {
				f->R.rax = -1;
				break;
			}

			struct fd_entry *entry = malloc (sizeof *entry);
			if (entry == NULL) {
				file_close (file);
				f->R.rax = -1;
				break;
			}

			int max_fd = 1;
			for (struct list_elem *e = list_begin (&cur->fd_table);
					e != list_end (&cur->fd_table);
					e = list_next (e)) {
				struct fd_entry *fd_entry =
					list_entry (e, struct fd_entry, file_elem);
				if (fd_entry->fd > max_fd)
					max_fd = fd_entry->fd;
			}

			entry->fd = max_fd + 1;
			entry->sfd = malloc (sizeof (struct shared_fd));
			entry->sfd->type = FILE_TYPE;
			entry->sfd->file = file;
			entry->sfd->shared_count = 1;
			list_push_back (&cur->fd_table, &entry->file_elem);
			f->R.rax = entry->fd;
			break;
		}
		case SYS_FILESIZE:
		{
			int fd = f->R.rdi;	
			struct fd_entry *entry = find_fd_entry(fd);
			if (entry == NULL || entry->sfd->type != FILE_TYPE) {
				f->R.rax = -1;
				break;
			}

			f->R.rax = file_length(entry->sfd->file);
			break;
		}
		case SYS_READ:
		{
			int fd = (int) f->R.rdi;
			char *buf = (char *) f->R.rsi;
			int size = (int) f->R.rdx;
			// 버퍼가 유효한지 체크. 
			// SYS_READ는 writable 해야하니까 writable = true로 넘겨줌.
			if (!is_valid_buffer (buf, size, true))
				sys_exit (-1);

			if (size == 0) {
				f->R.rax = 0;
				// 디버깅용
				// printf("read 0 bytes from fd %d\n", fd);
				break;
			}

			struct fd_entry *fd_entry = find_fd_entry (fd);
			if (fd_entry == NULL) {
				f->R.rax = -1;
				// 디버깅용
				// printf("fd_entry is NULL\n");
				break;
			}

			if (fd_entry->sfd->type == STDIN_FILENO) {
				for (int i = 0; i < size; i++)
					buf[i] = input_getc ();
				f->R.rax = size;
				// 디버깅용
				// printf("read %d bytes from fd %d\n", (int) f->R.rax, fd);
				break;
			}

			if (fd_entry->sfd->type == STDOUT_FILENO) {
				f->R.rax = -1;
				// 디버깅용
				// printf("attempted to read from stdout\n");
				break;
			}

			f->R.rax = file_read (fd_entry->sfd->file, buf, size);
			// 디버깅용	
			// printf("read %d bytes from fd %d\n", (int) f->R.rax, fd);
			break;
		}
		case SYS_WRITE:
		{
			int fd = (int) f->R.rdi;
			char *buf = (char *) f->R.rsi;
			int size = (int) f->R.rdx;
			// 버퍼가 유효한지 체크. 
			// SYS_WRITE는 read-only 해야하니까 writable = false로 넘겨줌.
			if (buf == NULL || !is_valid_buffer (buf, size, false))
				sys_exit (-1);

			struct fd_entry *entry = find_fd_entry (fd);
			if (entry == NULL) {
				f->R.rax = -1;
				// 디버깅용
				// printf("fd_entry is NULL\n");
				break;
			}
			if (entry->sfd->type == STDOUT_FILENO) {
				putbuf (buf, size);
				f->R.rax = size;
				// 디버깅용
				// printf("write %d bytes to fd %d\n", (int) f->R.rax, fd);
				break;
			}
			if (entry->sfd->type == FILE_TYPE)
			{
				f->R.rax = file_write(entry->sfd->file, buf, size);
				// 디버깅용
				// printf("write %d bytes to fd %d\n", (int) f->R.rax, fd);
			}
			break;
		}
		case SYS_SEEK:
		{
			int fd = (int) f->R.rdi;
			unsigned pos = (unsigned) f->R.rsi;
			if (pos < 0) {
				pos = 0;
			}
			struct fd_entry *fd_entry = find_fd_entry (fd);
			if (fd_entry == NULL) {
				break;
			}
			if (fd_entry->sfd->type == FILE_TYPE)
			{
				file_seek (fd_entry->sfd->file, pos);
			}
			break;
		}
		case SYS_TELL:
		{
			int fd = (int) f->R.rdi;
			struct fd_entry *fd_entry = find_fd_entry (fd);
			if (fd_entry == NULL) {
				f->R.rax = -1;
				break;
			}
			if (fd_entry->sfd->type == FILE_TYPE)
			{
				f->R.rax = file_tell (fd_entry->sfd->file);
			}
			break;
		}
		case SYS_CLOSE:
		{
			int fd = f->R.rdi;
			struct fd_entry *fd_entry = find_fd_entry (fd);
			if (fd_entry == NULL) {
				f->R.rax = -1;
				break;
			}
			list_remove (&fd_entry->file_elem);
			fd_entry->sfd->shared_count--;
			if (fd_entry->sfd->shared_count == 0)
			{
				if (fd_entry->sfd->type == FILE_TYPE)
					file_close (fd_entry->sfd->file);
				free (fd_entry->sfd);
			}
			free (fd_entry);
			break;

		}
		case SYS_DUP2:
		{
			int fd_1 = f->R.rdi;
			int fd_2 = f->R.rsi;
			struct fd_entry *fd_entry_1 = find_fd_entry (fd_1);
			if (fd_entry_1 == NULL) {
				f->R.rax = -1;
				break;
			}
			struct fd_entry *fd_entry_2 = find_fd_entry (fd_2);
			if ( fd_entry_2 != NULL && fd_entry_1->sfd == fd_entry_2->sfd )
			{
				f->R.rax = fd_entry_2->fd;
				break;
			}
			if (fd_entry_2 == NULL) 
			{
				struct fd_entry *entry = malloc (sizeof *entry);
				struct thread *cur = thread_current();
				if (entry == NULL) {
					f->R.rax = -1;
					break;
				}
				entry->fd = fd_2;
				entry->sfd = fd_entry_1->sfd;
				entry->sfd->shared_count++;
				list_push_back (&cur->fd_table, &entry->file_elem);
				f->R.rax = entry->fd;	

			}
			else
			{
				fd_entry_2->sfd->shared_count--;
				if (fd_entry_2->sfd->shared_count == 0)
				{
					if (fd_entry_2->sfd->type == FILE_TYPE)
						file_close (fd_entry_2->sfd->file);
					free (fd_entry_2->sfd);
				}
				fd_entry_2->sfd = fd_entry_1->sfd;
				fd_entry_2->sfd->shared_count++;
				f->R.rax = fd_entry_2->fd;
			}
			
			break;

		}
		case SYS_MMAP:
		{
			void *addr = f->R.rdi;
			size_t length = f->R.rsi;
			int writable = f->R.rdx;
			int fd = f->R.r10;
			off_t offset = f->R.r8;

			if (!is_user_vaddr (addr)) 
				goto fail;
			if (length == NULL)
				goto fail;
			if (fd == STDIN_FILENO || fd == STDOUT_FILENO) 
				goto fail;

			pg_round_down (addr);

			struct fd_entry *fd_entry = find_fd_entry (fd);
			struct file *file = fd_entry->sfd->file;
			do_mmap (addr, length, writable, file, offset);

			break;

			fail:
				f->R.rax = -1;
				break;
		}
		default:
			sys_exit (-1);
			break;
	}
}

static struct fd_entry *
find_fd_entry (int fd) {
	struct thread *cur = thread_current ();
	for (struct list_elem *e = list_begin (&cur->fd_table);
			e != list_end (&cur->fd_table);
			e = list_next (e)) {
		struct fd_entry *fd_entry = list_entry (e, struct fd_entry, file_elem);
		if (fd_entry->fd == fd)
			return fd_entry;
	}

	return NULL;
}
