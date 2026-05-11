#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "userprog/fd.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *ii);
static void __do_fork (void *);
static bool is_file_fd (const struct fd_entry *entry);

/* initd와 다른 프로세스를 위한 일반 프로세스 초기화 함수입니다. */
static void
process_init (void) {
	struct thread *current = thread_current ();

	struct fd_entry *stdin_fd = malloc (sizeof (struct fd_entry));
	if (stdin_fd != NULL) {
		stdin_fd->fd = STDIN_FILENO;
		stdin_fd->sfd = malloc (sizeof (struct shared_fd));
		if (stdin_fd->sfd == NULL) {
			free (stdin_fd);
		}
		else {
			stdin_fd->sfd->type = STDIN_FILENO;
			stdin_fd->sfd->shared_count = 1;
			stdin_fd->sfd->file = NULL;
			list_push_back (&current->fd_table, &stdin_fd->file_elem);
		}
	}
	struct fd_entry *stdout_fd = malloc (sizeof (struct fd_entry));
	if (stdout_fd != NULL) {
		stdout_fd->fd = STDOUT_FILENO;
		stdout_fd->sfd = malloc (sizeof (struct shared_fd));
		if (stdout_fd->sfd == NULL) {
			free (stdout_fd);
		}
		else {
			stdout_fd->sfd->type = STDOUT_FILENO;
			stdout_fd->sfd->shared_count = 1;
			stdout_fd->sfd->file = NULL;
			list_push_back (&current->fd_table, &stdout_fd->file_elem);
		}
	}
}

static bool
is_file_fd (const struct fd_entry *entry) {
	return entry->sfd->type == FILE_TYPE;
}

struct initd_info {
	void * f_name;
	struct child_status *cs;
};

struct child_status *
init_child_status (struct thread* current) {
	/* 현재 스레드의 children 리스트에 새로 만들 스레드를 등록할 준비를 합니다. */
	struct child_status *cs = malloc(sizeof(struct child_status));
	if (cs == NULL) {
		return NULL;
	}
	cs->tid = -1;
	cs->waited = false;
	cs->exited = false;
	cs->parent_alive = true; //init_child_status()로 만든 자식 기록지에 “부모는 살아있다”를 기본값으로 넣는 것.
	cs->exit_code = -1;
	sema_init (&cs->wait_sema, 0);
	list_push_back (&thread_current ()->children, &cs->elem);

	return cs;
}

/* FILE_NAME에서 로드되는 "initd"라는 첫 사용자 프로그램을 시작합니다.
 * 새 스레드는 process_create_initd()가 반환되기 전에 스케줄될 수 있고,
 * 심지어 종료될 수도 있습니다. initd의 thread id를 반환하며,
 * 스레드를 만들 수 없으면 TID_ERROR를 반환합니다.
 * 이 함수는 반드시 한 번만 호출해야 합니다. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* FILE_NAME의 복사본을 만듭니다.
	 * 그렇지 않으면 호출자와 load() 사이에 경쟁 조건이 생깁니다. */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	char process_name[16] = { 0 };
	char *save_ptr;
	strlcpy(process_name, file_name, sizeof process_name);
	strtok_r(process_name, " ", &save_ptr);

	struct child_status *new_cs = init_child_status (thread_current ());
	
	struct initd_info *ii = malloc (sizeof (struct initd_info));
	if (ii == NULL) {
		list_remove (&new_cs->elem);
		free (new_cs);
		return TID_ERROR;
	}
	ii->cs = new_cs;
	ii->f_name = fn_copy;


	/* FILE_NAME을 실행할 새 스레드를 만듭니다. */
	tid = thread_create (process_name, PRI_DEFAULT, initd, ii);
	if (tid == TID_ERROR) {
		palloc_free_page (fn_copy);
		list_remove (&new_cs->elem);
		free (new_cs);
		free(ii);
	} else {
		new_cs->tid = tid;
	}
		
	return tid;
}

/* 첫 사용자 프로세스를 실행하는 스레드 함수입니다. */
static void
initd (void* ii) {
	char *f_name = ((struct initd_info *) ii)->f_name;
	struct child_status *cs = ((struct initd_info *) ii)->cs;
	thread_current ()->wait_status = cs;
	free(ii);
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

struct fork_info {
	struct thread * t;
	struct intr_frame *if_;
	struct child_status *cs;
};

/* 현재 프로세스를 `name`으로 복제합니다. 새 프로세스의 thread id를 반환하며,
 * 스레드를 만들 수 없으면 TID_ERROR를 반환합니다. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* 현재 스레드를 새 스레드로 복제합니다. */
	struct fork_info *fi = malloc (sizeof (struct fork_info));
	if (fi == NULL) {
		return TID_ERROR;
	}
	fi->if_ = malloc (sizeof (struct intr_frame));
	if (fi->if_ == NULL) {
		free(fi);
		return TID_ERROR;
	}
	memcpy (fi->if_, if_, sizeof (struct intr_frame));
	fi->t = thread_current ();
	ASSERT(fi->t != NULL);
	ASSERT(fi->t->pml4 != NULL);


	struct child_status *cs = malloc (sizeof (struct child_status));
	if (cs == NULL)  {
		free (fi->if_);
		free (fi);
		return TID_ERROR;
	}
	fi->cs = cs;
	cs->tid = -1;
	cs->waited = false;
	cs->exited = false;
	cs->parent_alive = true; //init_child_status()로 만든 자식 기록지에 “부모는 살아있다”를 기본값으로 넣는 것.
	cs->exit_code = -1; // fork로 자식 기록지 cs 만들었을 때, 기본 종료값 -1설정. 자식이 비정상 종료 되었을때 대비한 기본값.
	sema_init (&cs->wait_sema, 0);
	list_push_back (&thread_current ()->children, &cs->elem);
	
	tid_t tid = thread_create (name, PRI_DEFAULT, __do_fork, fi);
	if (tid == TID_ERROR) {
		list_remove (&cs->elem);
		free (cs);
		free (fi->if_);
		free (fi);
		return TID_ERROR;
	}
	cs->tid = tid;
	
	sema_down (&cs->wait_sema);
	if (cs->tid == TID_ERROR) {
		return TID_ERROR;
	}
	return tid;
}

#ifndef VM
/* 이 함수를 pml4_for_each에 넘겨 부모의 주소 공간을 복제합니다.
 * 이 코드는 Project 2에서만 사용됩니다. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	// printf("%p, 커널 pte인가? %d ,%p: va가 사용자 가상 주소인가? %d\n", *pte, is_kern_pte(pte),va, is_user_vaddr(va));

	/* 1. TODO: parent_page가 커널 페이지라면 즉시 반환합니다. */
	if (is_kern_pte (pte)) {
		return true;
	}

	/* 2. 부모의 4단계 페이지 맵에서 VA를 찾습니다. */
	
	parent_page = pml4_get_page (parent->pml4, va);
	if (parent_page == NULL 
		// || ((uint64_t) parent_page & 0x001) == 0
	) {
		// printf ("부모 페이지를 찾지 못했습니다...\n");
		return true;
	}

	/* 3. TODO: 자식을 위한 새 PAL_USER 페이지를 할당하고 결과를
	 *    TODO: NEWPAGE에 저장합니다. */
	newpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (newpage == NULL) {
		printf ("newpage allocation fail...\n");
		return false;
	}

	/* 4. TODO: 부모의 페이지를 새 페이지에 복사하고,
	 *    TODO: 부모의 페이지가 쓰기 가능한지 확인합니다. 그 결과에 따라
	 *    TODO: WRITABLE을 설정합니다. */
	memcpy (newpage, parent_page, PGSIZE);
	writable = is_writable (pte);

	/* 5. WRITABLE 권한으로 주소 VA에 새 페이지를 자식의 페이지 테이블에
	 *    추가합니다. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: 페이지 삽입에 실패하면 오류 처리를 합니다. */
		palloc_free_page (newpage);
		return false;
	}
	return true;
}
#endif

/* 부모의 실행 컨텍스트를 복사하는 스레드 함수입니다.
 * 힌트) parent->tf에는 프로세스의 userland 컨텍스트가 없습니다.
 *       즉, process_fork의 두 번째 인자를 이 함수에 넘겨야 합니다. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread*) (((struct fork_info *) aux)->t);
	struct thread *current = thread_current ();
	struct intr_frame *parent_if = ((struct fork_info *) aux)->if_;
	struct child_status *cs = ((struct fork_info *) aux)->cs;
	

	current->wait_status = cs;
	bool succ = true;

	/* 1. CPU 컨텍스트를 로컬 스택으로 복사합니다. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	free (parent_if);
	free (aux);

	/* 2. 페이지 테이블을 복제합니다. */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	ASSERT(parent != NULL);
	ASSERT(parent->pml4 != NULL);
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: 여기에 코드를 작성합니다.
	 * TODO: 힌트) 파일 객체를 복제하려면 include/filesys/file.h의
	 * TODO:       `file_duplicate`를 사용합니다. 이 함수가 부모의 자원을
	 * TODO:       성공적으로 복제하기 전까지 부모는 fork()에서 반환하면 안 됩니다. */

	ASSERT (list_empty (&current->fd_table));
	struct list_elem *e;
	for (	
			e = list_begin (&parent->fd_table); 
			e != list_end (&parent->fd_table); 
			e = list_next (e)
		) 
	{
		struct fd_entry *fde = list_entry (e, struct fd_entry, file_elem);
		struct fd_entry *new_fde = malloc (sizeof (struct fd_entry));
		if (new_fde == NULL) {  // TODO. 여기서 기존에 만들어놨던 fde를 정리 안 하면 메모리 누수 아님?
			struct list *fd_table = &current->fd_table;
			while (!list_empty(fd_table)) {
				e = list_begin(fd_table);
				struct fd_entry *entry = list_entry(e, struct fd_entry, file_elem);
				list_remove(e);
				if (is_file_fd (entry))
					file_close (entry->sfd->file);
				free (entry->sfd);
				free(entry);
			}
			cs->tid = TID_ERROR;
			sema_up (&cs->wait_sema);
			thread_exit ();
		}
		new_fde->fd = fde->fd;
		new_fde->sfd = malloc (sizeof (struct shared_fd));
		if (new_fde->sfd == NULL) {
			free (new_fde);
			thread_exit ();
		}
		new_fde->sfd->file = NULL;
		if (is_file_fd (fde))
			new_fde->sfd->file = file_duplicate (fde->sfd->file);
		new_fde->sfd->shared_count = 1;
		new_fde->sfd->type = fde->sfd->type;
		list_push_back (&current->fd_table, &new_fde->file_elem);
	}

	process_init ();

	/* 마지막으로 새로 생성한 프로세스로 전환합니다. */

	if_.R.rax = 0;
	sema_up(&cs->wait_sema);
	if (succ)
		do_iret (&if_);
error:
	cs->tid = TID_ERROR;
	sema_up (&cs->wait_sema);
	thread_exit ();
}

/* 현재 실행 컨텍스트를 f_name으로 전환합니다.
 * 실패하면 -1을 반환합니다. */
int
process_exec (void *f_name) {

    if (f_name == NULL)
		return -1;
    
	char *cmd_page = f_name;
	char *arg;

	bool success;

	/* thread 구조체의 intr_frame을 사용할 수 없습니다.
	* 현재 스레드가 다시 스케줄될 때 실행 정보가 그 멤버에 저장되기 때문입니다. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	int arg_count = 0;
	char *argv_tokens[32] = { 0 };
	uint64_t arg_addrs[32] = { 0 };
	char *next_ptr;
	int token_size = 0;

	arg = strtok_r(f_name, " ", &next_ptr);

	/* 먼저 현재 컨텍스트를 정리합니다. */
	process_cleanup ();

	/* 그 다음 실행 파일을 로드합니다. */
	success = load (arg, &_if);
	
	if (success)
	{
		
		while(arg != NULL && arg_count < 32)
		{
			argv_tokens[arg_count++] = arg;
			arg = strtok_r (NULL, " ", &next_ptr);
		}

		for(int j = arg_count-1 ; j >= 0; j--)
		{
			token_size = strlen(argv_tokens[j]) + 1;
			_if.rsp -= token_size;
			arg_addrs[j] = _if.rsp;
			memcpy ((void*)_if.rsp, argv_tokens[j], token_size);
		}

		int j = _if.rsp % 8;
		_if.rsp -= j;
		memset ((void *) _if.rsp, 0, j);
		_if.rsp -= 8;
		memset ((void *) _if.rsp, 0, 8);

		for(int j = arg_count-1 ; j >= 0; j--)
		{
			_if.rsp -= 8;
			memcpy ((void *) _if.rsp, &arg_addrs[j], 8);
		}

		_if.R.rsi = _if.rsp;
		
		_if.rsp -= 8;
		memset ((void *) _if.rsp, 0, 8);

		_if.R.rdi = arg_count;

		/* 로드가 실패했으면 종료합니다. */
		palloc_free_page (cmd_page);

		/* 전환된 프로세스를 시작합니다. */
		do_iret (&_if);
		NOT_REACHED ();
	}

	if (!success){
		palloc_free_page (cmd_page);
		return -1;
	}
    
}


/* thread TID가 종료될 때까지 기다리고 그 종료 상태를 반환합니다.
 * 커널에 의해 종료되었다면, 즉 예외 때문에 죽었다면 -1을 반환합니다.
 * TID가 올바르지 않거나 호출 프로세스의 자식이 아니거나, 주어진 TID에 대해
 * process_wait()가 이미 성공적으로 호출된 적이 있다면 기다리지 않고 즉시 -1을
 * 반환합니다.
 *
 * 이 함수는 문제 2-2에서 구현됩니다. 지금은 아무 일도 하지 않습니다. */
int
process_wait (tid_t child_tid) {
	struct thread* cur = thread_current ();
	struct child_status *cs = NULL;
	for (struct list_elem *e = list_begin (&cur->children); 
		e != list_end(&cur->children); 
		e = list_next (e)) {
		cs = list_entry (e, struct child_status, elem);
		if (cs->tid == child_tid) {
			break;
		}
		cs = NULL;
	}

	if (cs == NULL) {
		return -1;
	}

	if (cs->waited) {
		return -1;
	}

	cs->waited = true;
	sema_down (&cs->wait_sema);

	/* 깨어난 뒤의 처리입니다. */
	list_remove (&cs->elem);
	int exit_code = cs->exit_code;
	free (cs);
	return exit_code;
}

/* 프로세스를 종료합니다. 이 함수는 thread_exit()에서 호출됩니다. */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: 여기에 코드를 작성합니다.
	 * TODO: 프로세스 종료 메시지를 구현합니다.
	 * TODO: project2/process_termination.html을 참고하세요.
	 * TODO: 프로세스 자원 정리는 여기에서 구현하는 것을 권장합니다. */
	
	struct list *fd_table = &curr->fd_table;
	struct list_elem *e = NULL;
	
	while (!list_empty(fd_table)) {
		e = list_begin(fd_table);
		struct fd_entry *entry = list_entry(e, struct fd_entry, file_elem);
		list_remove(e);
		entry->sfd->shared_count--;
		if (entry->sfd->shared_count == 0)
		{
			if (is_file_fd (entry))
				file_close (entry->sfd->file);
			free (entry->sfd);
		}
		free(entry);
	}
#ifdef USERPROG
	if ( curr->running_file != NULL ){	
		file_allow_write(curr->running_file);
		file_close(curr->running_file);	
		curr->running_file = NULL;
	}
	struct child_status *cs = curr->wait_status;

	if (cs != NULL) {
		if (curr->pml4 != NULL)
			printf("%s: exit(%d)\n", curr->name, cs->exit_code);
		
		cs->exited = true;
		sema_up (&cs->wait_sema);
	}




#endif
	process_cleanup ();
}

/* 현재 프로세스의 자원을 해제합니다. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* 현재 프로세스의 페이지 디렉터리를 제거하고 커널 전용 페이지 디렉터리로
	 * 되돌아갑니다. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* 여기서는 순서가 매우 중요합니다. 페이지 디렉터리를 전환하기 전에
		 * cur->pagedir를 NULL로 설정해야 타이머 인터럽트가 프로세스 페이지
		 * 디렉터리로 다시 전환하지 못합니다. 프로세스의 페이지 디렉터리를
		 * 제거하기 전에 기본 페이지 디렉터리를 활성화해야 합니다. 그렇지 않으면
		 * 이미 해제되고 지워진 페이지 디렉터리가 활성 상태로 남을 수 있습니다. */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* 다음 스레드에서 사용자 코드를 실행할 수 있도록 CPU를 설정합니다.
 * 이 함수는 컨텍스트 스위치마다 호출됩니다. */
void
process_activate (struct thread *next) {
	/* 스레드의 페이지 테이블을 활성화합니다. */
	pml4_activate (next->pml4);

	/* 인터럽트 처리에 사용할 스레드의 커널 스택을 설정합니다. */
	tss_update (next);
}

/* ELF 바이너리를 로드합니다. 다음 정의는 ELF 명세 [ELF1]에서 거의 그대로
 * 가져온 것입니다. */

/* ELF 타입입니다. [ELF1] 1-2를 참고하세요. */
#define EI_NIDENT 16

#define PT_NULL    0            /* 무시합니다. */
#define PT_LOAD    1            /* 로드 가능한 세그먼트입니다. */
#define PT_DYNAMIC 2            /* 동적 링킹 정보입니다. */
#define PT_INTERP  3            /* 동적 로더의 이름입니다. */
#define PT_NOTE    4            /* 보조 정보입니다. */
#define PT_SHLIB   5            /* 예약되어 있습니다. */
#define PT_PHDR    6            /* 프로그램 헤더 테이블입니다. */
#define PT_STACK   0x6474e551   /* 스택 세그먼트입니다. */

#define PF_X 1          /* 실행 가능합니다. */
#define PF_W 2          /* 쓰기 가능합니다. */
#define PF_R 4          /* 읽기 가능합니다. */

/* 실행 파일 헤더입니다. [ELF1] 1-4부터 1-8까지를 참고하세요.
 * ELF 바이너리의 맨 앞에 나타납니다. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* 약어 */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* FILE_NAME의 ELF 실행 파일을 현재 스레드에 로드합니다.
 * 실행 파일의 진입점을 *RIP에 저장하고 초기 스택 포인터를 *RSP에 저장합니다.
 * 성공하면 true를, 실패하면 false를 반환합니다. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* 페이지 디렉터리를 할당하고 활성화합니다. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* 실행 파일을 엽니다. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* 실행 파일 헤더를 읽고 검증합니다. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* 프로그램 헤더를 읽습니다. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* 이 세그먼트는 무시합니다. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* 일반 세그먼트입니다.
						 * 앞부분은 디스크에서 읽고 나머지는 0으로 채웁니다. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* 전체가 0입니다.
						 * 디스크에서 아무것도 읽지 않습니다. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* 스택을 설정합니다. */
	if (!setup_stack (if_))
		goto done;

	/* 시작 주소입니다. */
	if_->rip = ehdr.e_entry;

	/* TODO: 여기에 코드를 작성합니다.
	 * TODO: 인자 전달을 구현합니다. project2/argument_passing.html을 참고하세요. */
	success = true;
#ifdef USERPROG
	t->running_file = file;
	file_deny_write(file);
	file = NULL;	
#endif
done:

	/* 로드 성공 여부와 관계없이 여기로 도착합니다. */
	file_close (file);
	return success;
}


/* PHDR이 FILE 안의 유효하고 로드 가능한 세그먼트를 설명하는지 검사합니다.
 * 그렇다면 true를, 아니면 false를 반환합니다. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset과 p_vaddr은 같은 페이지 오프셋을 가져야 합니다. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset은 FILE 안을 가리켜야 합니다. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz는 최소한 p_filesz만큼 커야 합니다. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* 세그먼트는 비어 있으면 안 됩니다. */
	if (phdr->p_memsz == 0)
		return false;

	/* 가상 메모리 영역의 시작과 끝은 모두 사용자 주소 공간 범위 안에
	   있어야 합니다. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* 이 영역은 커널 가상 주소 공간을 가로질러 감싸면 안 됩니다. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* 0번 페이지 매핑을 허용하지 않습니다.
	   0번 페이지를 매핑하는 것은 좋지 않을 뿐 아니라, 이를 허용하면
	   시스템 콜에 널 포인터를 넘기는 사용자 코드가 memcpy() 등의
	   널 포인터 assertion을 통해 커널 패닉을 일으킬 가능성이 큽니다. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* 문제없습니다. */
	return true;
}

#ifndef VM
/* 이 블록의 코드는 Project 2에서만 사용됩니다.
 * Project 2 전체에서 사용할 함수를 구현하려면 #ifndef 매크로 밖에
 * 구현하세요. */

/* load() 헬퍼 함수입니다. */
static bool install_page (void *upage, void *kpage, bool writable);

/* FILE의 OFS 오프셋에서 시작하는 세그먼트를 UPAGE 주소에 로드합니다.
 * 전체적으로 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리를 다음과 같이
 * 초기화합니다.
 *
 * - UPAGE 위치의 READ_BYTES 바이트는 FILE의 OFS 오프셋부터 읽어야 합니다.
 *
 * - UPAGE + READ_BYTES 위치의 ZERO_BYTES 바이트는 0으로 채워야 합니다.
 *
 * 이 함수가 초기화한 페이지들은 WRITABLE이 true이면 사용자 프로세스가 쓸 수
 * 있어야 하며, 그렇지 않으면 읽기 전용이어야 합니다.
 *
 * 성공하면 true를 반환하고, 메모리 할당 오류나 디스크 읽기 오류가 발생하면
 * false를 반환합니다. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* 이 페이지를 어떻게 채울지 계산합니다.
		 * FILE에서 PAGE_READ_BYTES 바이트를 읽고,
		 * 마지막 PAGE_ZERO_BYTES 바이트는 0으로 채웁니다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* 메모리 페이지를 가져옵니다. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* 이 페이지를 로드합니다. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* 프로세스의 주소 공간에 페이지를 추가합니다. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* 다음 페이지로 이동합니다. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* USER_STACK에 0으로 채운 페이지를 매핑해 최소 스택을 만듭니다. */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* 페이지 테이블에 사용자 가상 주소 UPAGE에서 커널 가상 주소 KPAGE로 가는
 * 매핑을 추가합니다.
 * WRITABLE이 true이면 사용자 프로세스가 페이지를 수정할 수 있고,
 * 그렇지 않으면 읽기 전용입니다.
 * UPAGE는 아직 매핑되어 있으면 안 됩니다.
 * KPAGE는 보통 palloc_get_page()로 사용자 풀에서 얻은 페이지여야 합니다.
 * 성공하면 true를 반환하고, UPAGE가 이미 매핑되어 있거나 메모리 할당에
 * 실패하면 false를 반환합니다. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* 해당 가상 주소에 이미 페이지가 없는지 확인한 뒤, 그 위치에 페이지를
	 * 매핑합니다. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* 여기서부터의 코드는 Project 3 이후에 사용됩니다.
 * Project 2에서만 사용할 함수를 구현하려면 위쪽 블록에 구현하세요. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: 파일에서 세그먼트를 로드합니다. */
	/* TODO: 이 함수는 주소 VA에서 첫 page fault가 발생했을 때 호출됩니다. */
	/* TODO: 이 함수를 호출할 때 VA를 사용할 수 있습니다. */
}

/* FILE의 OFS 오프셋에서 시작하는 세그먼트를 UPAGE 주소에 로드합니다.
 * 전체적으로 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리를 다음과 같이
 * 초기화합니다.
 *
 * - UPAGE 위치의 READ_BYTES 바이트는 FILE의 OFS 오프셋부터 읽어야 합니다.
 *
 * - UPAGE + READ_BYTES 위치의 ZERO_BYTES 바이트는 0으로 채워야 합니다.
 *
 * 이 함수가 초기화한 페이지들은 WRITABLE이 true이면 사용자 프로세스가 쓸 수
 * 있어야 하며, 그렇지 않으면 읽기 전용이어야 합니다.
 *
 * 성공하면 true를 반환하고, 메모리 할당 오류나 디스크 읽기 오류가 발생하면
 * false를 반환합니다. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* 이 페이지를 어떻게 채울지 계산합니다.
		 * FILE에서 PAGE_READ_BYTES 바이트를 읽고,
		 * 마지막 PAGE_ZERO_BYTES 바이트는 0으로 채웁니다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: lazy_load_segment에 정보를 전달하도록 aux를 설정합니다. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* 다음 페이지로 이동합니다. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* USER_STACK에 스택 페이지를 만듭니다. 성공하면 true를 반환합니다. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: stack_bottom에 스택을 매핑하고 즉시 페이지를 claim합니다.
	 * TODO: 성공하면 rsp를 그에 맞게 설정합니다.
	 * TODO: 해당 페이지가 스택임을 표시해야 합니다. */
	/* TODO: 여기에 코드를 작성합니다. */

	return success;
}
#endif /* VM */
