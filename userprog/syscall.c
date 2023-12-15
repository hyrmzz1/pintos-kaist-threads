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
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

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
void check_address (void *addr);
const int STDIN = 0;
const int STDOUT = 1;

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
		// 	f->R.rax = fork ();
		// 	break;
		// case SYS_EXEC :
		// 	f->R.rax = exec ();	
		// 	break;
		// case SYS_WAIT :
		// 	f->R.rax = wait ();	
		// 	break;
		case SYS_CREATE :
			f->R.rax = create (f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE :
			f->R.rax = remove (f->R.rdi);
			break;
		case SYS_OPEN :
			f->R.rax = open (f->R.rdi);
			break;
		case SYS_FILESIZE :
			f->R.rax = filesize (f->R.rdi);
			break;
		// case SYS_READ :
		// 	f->R.rax = read ();
		// 	break;
		case SYS_WRITE :	
			f->R.rax = write (f->R.rdi, f->R.rsi,f->R.rdx);
			break;
		// case SYS_SEEK :
		// 	f->R.rax = seek ();
		// 	break;
		// case SYS_TELL :
		// 	f->R.rax = tell ();
		// 	break;
		case SYS_CLOSE :	
			close (f->R.rdi);	// void니까 레지스터에 할당 X
			break;
		default:
			break;
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
	check_address(file);

	// 성공적으로 파일 생성(success) => true 반환, 파일 생성 실패(!success) => false 반환
	return filesys_create(file, initial_size);
}

/* parameter이라는 이름의 파일 삭제 (파일 open, close 상태와 무관) */
// remove != close
bool
remove (const char *file) {
	check_address(file);

	// 성공적으로 파일 삭제(success) => true 반환, 파일 삭제 실패(!success) => false 반환
	return filesys_remove(file);
}

/* 주소 값이 유저 영역에서 사용하는 주소 값인지 확인 */
void check_address (void *addr){
	struct thread *curr = thread_current();
	// 주소 값이 유저 영역 벗어난 영역 => exit(-1)	// 프로세스 종료
	if (is_kernel_vaddr(addr) || addr == NULL || pml4_get_page(curr->pml4, addr) == NULL)	// addr가 유저 영역에 위치하지 않거나 NULL이거나 매핑된 물리 메모리 주소가 없다면
		exit(-1);	// 프로세스 종료
}

/* parameter이라는 이름을 가진 파일 열기  */
int
open (const char *file) {
	check_address(file);
	struct file *newfile = filesys_open(file);	// 파일 open
	if (newfile == NULL)	// 파일 열기 실패
		return -1;	// int type이니까

	// 예외 처리 - 파일 열었는데 fd에 파일 넣을 수 없는 경우 => close() 해주기

	return process_add_file(newfile);	// 파일에 fd 부여 - 0(STDIN),1(STDOUT),2(STDERR)는 안됨
}

/* fd로서 열려있는 파일의 크기 반환 (바이트 단위) */
int
filesize (int fd) {
	struct file *open_file = process_get_file(fd);	// fd 이용하여 파일 객체 주소 리턴 -> 이거 열려있는 파일인가?
	if(open_file)
		return file_length(open_file);
	else	// 파일 존재하지 않으면
		return -1;
}

/* buffer 안에 fd로 열려있는 파일로부터 size 바이트 읽고, 실제 읽은 바이트 수 반환 */
int
read (int fd, void *buffer, unsigned size) {
	check_address(buffer);	// 주소값 유효한지 확인	(파라미터로는 포인터인 버퍼 넣기)
	if (fd < 0 || fd >= FD_LIMIT)	// fd 범위 체킹
		return -1;
	struct thread *curr = thread_current();
	struct file *fileobj = curr->fd_table[fd];
	if (fileobj == NULL)	// 파일 읽기 실패 (파일 끝인 경우 아님)
		return -1;

	int read_size = file_read(fileobj, buffer, size);
}

/* buffer에서 open file fd로부터 size 바이트 쓰기 */
int
write (int fd, const void *buffer, unsigned size) {
	// 실제로 쓰여진 바이트 수 반환
	// 일부 바이트 쓰이지 않은 경우 size 보다 작을 수 있음
	// 파일 확장 구현 X.
	check_address(buffer);
	putbuf(buffer, size);
	return size;
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
	process_close_file(fd);	// 해당 fd 닫고 fd table에서 해당 entry 초기화
	return;
}

int
dup2 (int oldfd, int newfd){
}
