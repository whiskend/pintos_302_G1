#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "intrinsic.h"

/* 처리한 page fault 수입니다. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* 사용자 프로그램에 의해 발생할 수 있는 인터럽트의 핸들러를 등록합니다.

   실제 Unix 계열 OS에서는 [SV-386] 3-24와 3-25에 설명된 것처럼
   대부분의 인터럽트를 signal 형태로 사용자 프로세스에 전달합니다.
   하지만 여기서는 signal을 구현하지 않습니다. 대신 사용자 프로세스를
   단순히 종료하도록 만듭니다.

   Page fault는 예외입니다. 여기서는 다른 예외와 같은 방식으로 처리하지만,
   가상 메모리를 구현하려면 이 부분을 바꿔야 합니다.

   각 예외에 대한 설명은 [IA32-v3a] 5.15절 "Exception and Interrupt
   Reference"를 참고하세요. */
void
exception_init (void) {
	/* 이 예외들은 사용자 프로그램이 명시적으로 발생시킬 수 있습니다.
	   예를 들어 INT, INT3, INTO, BOUND 명령어를 통해 발생시킬 수 있습니다.
	   따라서 DPL==3으로 설정하여 사용자 프로그램이 이 명령어들로
	   예외를 호출할 수 있게 합니다. */
	intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
	intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
	intr_register_int (5, 3, INTR_ON, kill,
			"#BR BOUND Range Exceeded Exception");

	/* 이 예외들은 DPL==0이므로 사용자 프로세스가 INT 명령어로 직접
	   호출할 수 없습니다. 하지만 간접적으로는 발생할 수 있습니다.
	   예를 들어 #DE는 0으로 나눌 때 발생할 수 있습니다. */
	intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
	intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
	intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
	intr_register_int (7, 0, INTR_ON, kill,
			"#NM Device Not Available Exception");
	intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
	intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
	intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
	intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
	intr_register_int (19, 0, INTR_ON, kill,
			"#XF SIMD Floating-Point Exception");

	/* 대부분의 예외는 인터럽트가 켜진 상태에서 처리할 수 있습니다.
	   Page fault는 fault 주소가 CR2에 저장되고 이 값을 보존해야 하므로
	   인터럽트를 끈 상태에서 처리해야 합니다. */
	intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* 예외 통계를 출력합니다. */
void
exception_print_stats (void) {
	printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* 사용자 프로세스가 일으켰을 가능성이 높은 예외의 핸들러입니다. */
static void
kill (struct intr_frame *f) {
	/* 이 인터럽트는 사용자 프로세스가 일으켰을 가능성이 큽니다.
	   예를 들어 프로세스가 매핑되지 않은 가상 메모리에 접근하려 했을 수
	   있습니다. 이것이 page fault입니다. 지금은 사용자 프로세스를 단순히
	   종료합니다. 나중에는 커널에서 page fault를 처리해야 합니다.
	   실제 Unix 계열 운영체제는 대부분의 예외를 signal로 프로세스에
	   돌려보내지만, 여기서는 signal을 구현하지 않습니다. */

	/* interrupt frame의 code segment 값은 예외가 어디에서 시작되었는지
	   알려줍니다. */
	switch (f->cs) {
		case SEL_UCSEG:
			/* 사용자의 code segment이므로 예상대로 사용자 예외입니다.
			   사용자 프로세스를 종료합니다. */
			printf ("%s: dying due to interrupt %#04llx (%s).\n",
					thread_name (), f->vec_no, intr_name (f->vec_no));
			intr_dump_frame (f);
			thread_exit ();

		case SEL_KCSEG:
			/* 커널의 code segment입니다. 이는 커널 버그를 의미합니다.
			   커널 코드는 예외를 던지면 안 됩니다. Page fault가 커널
			   예외를 일으킬 수는 있지만, 여기까지 도착해서는 안 됩니다.
			   이 상황을 드러내기 위해 커널 패닉을 발생시킵니다. */
			intr_dump_frame (f);
			PANIC ("Kernel bug - unexpected interrupt in kernel");

		default:
			/* 다른 code segment라면 발생해서는 안 되는 상황입니다.
			   커널 패닉을 발생시킵니다. */
			printf ("Interrupt %#04llx (%s) in unknown segment %04x\n",
					f->vec_no, intr_name (f->vec_no), f->cs);
			thread_exit ();
	}
}

/* Page fault 핸들러입니다. 가상 메모리를 구현하려면 이 뼈대 코드를
   채워야 합니다. Project 2의 일부 풀이에서도 이 코드를 수정해야 할 수
   있습니다.

   진입 시 fault가 난 주소는 CR2(Control Register 2)에 들어 있고,
   fault에 대한 정보는 exception.h의 PF_* 매크로가 설명하는 형식으로
   F의 error_code 멤버에 들어 있습니다. 아래 예제 코드는 그 정보를
   해석하는 방법을 보여줍니다. 이 둘에 대한 자세한 내용은 [IA32-v3a]
   5.15절 "Exception and Interrupt Reference"의 "Interrupt 14--Page
   Fault Exception (#PF)" 설명을 참고하세요. */
static void
page_fault (struct intr_frame *f) {
	bool not_present;  /* true: 페이지가 없음, false: 읽기 전용 페이지에 쓰기. */
	bool write;        /* true: 쓰기 접근, false: 읽기 접근. */
	bool user;         /* true: 사용자 접근, false: 커널 접근. */
	void *fault_addr;  /* Fault가 난 주소입니다. */

	/* fault를 일으킨 접근 대상인 가상 주소를 얻습니다.
	   이 주소는 코드나 데이터를 가리킬 수 있습니다.
	   fault를 일으킨 명령어의 주소와 반드시 같지는 않습니다.
	   명령어 주소는 f->rip입니다. */

	fault_addr = (void *) rcr2();

	/* 인터럽트를 다시 켭니다. 인터럽트를 껐던 이유는 CR2가 바뀌기 전에
	   확실히 읽기 위해서뿐입니다. */
	intr_enable ();


	/* 원인을 확인합니다. */
	not_present = (f->error_code & PF_P) == 0;
	write = (f->error_code & PF_W) != 0;
	user = (f->error_code & PF_U) != 0;

#ifdef VM
	/* Project 3 이후에서 사용합니다. */
	if (vm_try_handle_fault (f, fault_addr, user, write, not_present))
		return;
#endif

#ifdef USERPROG
	if ( user == true )
	{
		struct child_status *cs = thread_current ()->wait_status;
		if (cs != NULL)
			cs->exit_code = -1;
		thread_exit();
	}
#endif

	/* Page fault 수를 셉니다. */
	page_fault_cnt++;

	/* 실제 fault라면 정보를 출력하고 종료합니다. */
	printf ("Page fault at %p: %s error %s page in %s context.\n",
			fault_addr,
			not_present ? "not present" : "rights violation",
			write ? "writing" : "reading",
			user ? "user" : "kernel");
	kill (f);
}
