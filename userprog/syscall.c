#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "userprog/process.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

/* Map region identifier. */
typedef int off_t;
#define MAP_FAILED ((void *) NULL)

/* Maximum characters in a filename written by readdir(). */
#define READDIR_MAX_LEN 14

void halt (void);
void exit (int status);
pid_t fork (const char *thread_name);
int exec (const char *file);
int wait (pid_t pid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
int dup2 (int oldfd, int newfd);

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

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	switch (f->R.rax)
	{
		case SYS_HALT :
			halt ();
			break;	
		case SYS_EXIT :
			exit (f->R.rdi);	
			break;
		// case SYS_FORK :
		// 	fork ();
		// 	break;
		// case SYS_EXEC :
		// 	exec ();	
		// 	break;
		// case SYS_WAIT :
		// 	wait ();	
		// 	break;
		// case SYS_CREATE :
		// 	create ();		
		// 	break;
		// case SYS_REMOVE :
		// 	remove ();
		// 	break;
		// case SYS_OPEN :
		// 	open ();
		// 	break;
		// case SYS_FILESIZE :
		// 	filesize ();
		// 	break;
		// case SYS_READ :
		// 	read ();
		// 	break;
		case SYS_WRITE :	
			f->R.rax = write (f->R.rdi, f->R.rsi,f->R.rdx);
			break;
		// case SYS_SEEK :
		// 	seek ();
		// 	break;
		// case SYS_TELL :
		// 	tell ();
		// 	break;
		// case SYS_CLOSE :	
		// 	close ();
		// 	break;
		// default:
		// 	break;
	}
	// printf ("system call!\n");
	// thread_exit ();
}

/* power_off() 호출해서 Pintos 종료 */
void
halt (void) {
	power_off();
	// void type => 리턴 안해도 됨.
}

/* 현재 동작 중인 유저 프로그램 종료 + 커널에 상태 리턴 */
void
exit (int status) {
	struct thread *curr = thread_current();	// 현재 동작 중인 쓰레드 (프로세스라고 봐도 무방)
	curr->exit_status = status;	// status가 0이면 성공, 그 외의 값이면 에러.
	printf("%s: exit(%d)\n", thread_name(), status);
	thread_exit();
}

/* 현재 프로세스의 복제본 생성 */
pid_t
fork (const char *thread_name){
	// 자식 프로세스의 pid 반환

}

/* 주어진 char *에 있는 이름의 실행 파일로 현재 프로세스 변경 + 주어진 인자 전달 */
int
exec (const char *file) {
	// 성공적으로 진행된다면 아무 것도 반환 X
	// 프로그램이 이 프로세스 로드X or 실행 X => exit state -1 반환, 프로세스 종료.
	// 이 함수를 호출한 쓰레드의 이름은 바꾸지 않음
	// exec 함수 호출시 fd는 열린 상태
}

/* 자식 프로세스 pid 대기 + 자식의 종료 상태 검색 */
int
wait (pid_t pid) {
	// pid 살아있는 경우 종료될 때까지 대기하고 pid가 exit() 호출한 상태 반환
	// pid가 exit() 호출하지 않았지만 커널에 의해 종료 =>  (pid, wait은)-1 반환
}

/* 새로운 파일 생성. 첫 번째 파라미터는 파일명, 두 번째 파라미터는 파일 크기 */
// create != open
bool
create (const char *file, unsigned initial_size) {
	// 성공적으로 파일 생성 => true 반환
	// 파일 생성 실패 => false 반환
}

/* parameter이라는 이름의 파일 삭제 */
// remove != close
bool
remove (const char *file) {
	// 성공적으로 파일 삭제 => true 반환
	// 파일 삭제 실패 => false 반환
	// 파일 열리거나 닫힌 상태와 무관하게 삭제 가능
}

/* parameter이라는 이름을 가진 파일 열기  */
int
open (const char *file) {
	// 성공적으로 파일 열림 => 0보다 크거나 같은 정수(fd) 반환
	// 파일 열기 실패 => -1 반환
}

/* fd로서 열려있는 파일의 크기 반환 (바이트 단위) */
int
filesize (int fd) {
}

/* buffer 안에 fd로 열려있는 파일로부터 size 바이트 읽고, 실제 읽은 바이트 수 반환 */
int
read (int fd, void *buffer, unsigned size) {
	// 파일 끝에서 읽기 시도 => 0 반환
	// 파일 읽기 실패 (파일 끝인 경우 아님) => -1 반환
}

/* buffer에서 open file fd로부터 size 바이트 쓰기 */
int
write (int fd, const void *buffer, unsigned size) {
	// 실제로 쓰여진 바이트 수 반환
	// 일부 바이트 쓰이지 않은 경우 size 보다 작을 수 있음
	// 파일 확장 구현 X.
	printf("%s", buffer);	
	return 0;
}

/* open file fd에서 다음에 읽거나 쓸 바이트를 파일의 시작부터 바이트로 표시된 position으로 변경 */
void
seek (int fd, unsigned position) {
	// position 0: 파일의 시작
}

/* open file fd에서 읽히거나 쓰일 다음 바이트의 position 반환*/
unsigned
tell (int fd) {
	// 파일 시작 지점부터 몇 바이트인지 표현됨
}

/* Close file descriptor fd. */
void
close (int fd) {
	//  Exiting or terminating a process => 프로세스의 열려있는 fd들 닫음.
}

int
dup2 (int oldfd, int newfd){
}
