#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"

#include "threads/flags.h"
#include "threads/synch.h"
#include "threads/init.h" 
#include "filesys/filesys.h"
#include "filesys/file.h" 
#include "userprog/gdt.h"
#include "intrinsic.h"

#define	STDIN_FILENO	0
#define	STDOUT_FILENO	1

#define MAX_FD_NUM	(1<<9)
void check_address(void *addr);
struct file *fd_to_struct_filep(int fd);
int add_file_to_fd_table(struct file *file);
void remove_file_from_fd_table(int fd);

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void halt (void);
void exit (int);
void close (int fd);
bool create (const char *file , unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int read(int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
int filesize(int fd);

void seek(int fd, unsigned position);
unsigned tell (int fd);

void close (int fd);
int exec(char *file_name);
pid_t fork (const char *thread_name, struct intr_frame *f);

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

// 전방 선언
void check_address(void *addr);

void halt (void);
void eixt(int status);



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

	lock_init(&filesys_lock);
}

/* The main system call interface */
// 시스템 콜 넘버에 해당하는 시스템 콜을 호출하며,
// 이를 통해 파일 작업, 프로세스 관리, 입출력 요청 등을 처리함
void
syscall_handler(struct intr_frame *f UNUSED) {
    int sys_number = f->R.rax;	// 인터럽트 프레임에서 시스템 콜 번호를 가져옴

    switch (sys_number) {	// 시스템 콜 번호에 따라 적절한 시스템 콜 함수를 호출함
        case SYS_HALT:
            halt();
            break;
        case SYS_EXIT:
            exit(f->R.rdi);
            break;
        case SYS_FORK:
            fork(f->R.rdi, f->R.rsi);
            break;
        case SYS_EXEC:
            exec(f->R.rdi);
            break;
        case SYS_WAIT:
            wait(f->R.rdi);
            break;
        case SYS_CREATE:
            create(f->R.rdi, f->R.rsi);
            break;
        case SYS_REMOVE:
            remove(f->R.rdi);
            break;
        case SYS_OPEN:
            open(f->R.rdi);
            break;
        case SYS_FILESIZE:
            filesize(f->R.rdi);
            break;
        case SYS_READ:
            read(f->R.rdi, f->R.rsi, f->R.rdx);
            break;
        case SYS_WRITE:
            f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
            break;
        case SYS_SEEK:
            seek(f->R.rdi, f->R.rdx);
            break;
        case SYS_TELL:
            tell(f->R.rdi);
            break;
        case SYS_CLOSE:
            close(f->R.rdi);
            break;
        default:	// 정의되지 않은 시스템 콜 번호 처리 (break으로 switch문 종료)
            break;
    }
    printf("system call!\n");	// 시스템 콜 처리가 완료됨을 나타냄
    thread_exit();	// 시스템 콜 처리 후 현재 쓰레드를 종료
}

// 주소 값이 유저 영역에서 사용하는 주소 값인지 확인하는 함수
// 유저 영역을 벗어난 영역일 경우 프로세스 종료(exit(-1))
void check_address(void *addr) {
	struct thread *t = thread_current();	// 현재 실행 중인 쓰레드의 포인터를 가져옴

	if (!is_user_vaddr(addr)|| addr == NULL|| pml4_get_page(t->pml4, addr)== NULL)
	// addr의 유효성 검사
	// 주소가 유저 영역이 아닌 커널 영역에 있는 지 or NULL 포인터인지 or 페이지 테이블에 매핑되어 있지 않은지
	{
		exit(-1);	// 위 세 가지 조건에 하나라도 해당된다면 오류 코드 -1과 함께 종료
	}
}

// pintos 시스템을 종료시키는 함수
void halt(void)
{
	power_off();
}

// 현재 실행 중인 프로세스를 종료시키는 함수
void exit(int status)
{
	struct thread *t = thread_current();	// 현재 실행 중인 쓰레드를 가져옴
	printf("%s: exit(%d)\n", t->name, status);	// 프로세스 종료 메시지를 출력함 (name: 쓰레드의 이름, status : 쓰레드의 (종료)상태)
	thread_exit();	// 쓰레드를 종료하는 함수
}

// 파일 시스템에서 새로운 파일을 생성하는 함수
bool create(const char *file, unsigned initial_size)
{
	check_address(file);	// 인자로 들어온 파일 이름이 유효한 주소를 가리키는지 확인
    
	return filesys_create(file, initial_size);	// 파일 이름(file)과 초기 파일 크기(initial_size)를 인자로 받아 파일 생성 작업을 수행하며, 성공하면 true 실패하면 false를 반환함
}

// 파일 시스템에서 특정 파일을 제거하는 함수
bool remove(const char *file)
{
	check_address(file);	// 인자로 들어온 파일 이름이 유효한 주소를 가리키는지 확인
    
	return filesys_remove(file);	// 파일 이름(file)을 인자로 받아 제거하는 작업을 수행함 (성공하면 true, 실패하면 false 반환)
}

// 데이터를 특정 파일 또는 출력 스트림에 쓰는 함수
int write (int fd, const void *buffer, unsigned size) {
	// 주소 유효성 검사
	// 버퍼의 주소가 유효한지 확인
	check_address(buffer);
	// 파일 객체 가져오기
	// 파일 디스크립터 fd에 해당하는 파일 객체를 가져옴
	struct file *fileobj = fd_to_struct_filep(fd);

	int read_count;

	// 파일 시스템 락 획득
	// 파일 시스템에 대한 접근을 동기화하기 위해 락을 획득함
	lock_acquire(&filesys_lock);
	// 표준 출력에 쓰기
	// fd가 표준 출력일 경우, putbuf 함수를 사용해 버퍼의 내용을 출력함
	if (fd == STDOUT_FILENO) {
		putbuf(buffer, size);
		read_count = size;

	}	
	// 표준 입력에 쓰기 시도 시 실패 반환
	// fd가 표준 입력일 경우, -1을 반환하여 쓰기 작업이 불가능함을 나타냄
	else if (fd == STDIN_FILENO) {
		lock_release(&filesys_lock);
		return -1;
	}
	// 일반 파일에 쓰기
	// fd가 일반 파일일 경우, file_write 함수를 사용해 파일에 데이터를 씀
	else if (fd >= 2){
		
		if (fileobj == NULL) {
		lock_release(&filesys_lock);
		exit(-1);
		}

		read_count = file_write(fileobj, buffer, size);
		
	}
	// 파일 시스템 락 해제 및 반환
	lock_release(&filesys_lock);
	return read_count;
}


// 파일을 열고 파일 디스크립터를 반환하는 함수
int open (const char *file) {
	check_address(file); // 주어진 파일 주소가 유효한지 확인
	struct file *file_obj = filesys_open(file); // filesys_open으로 파일을 열고, 열린 파일 객체에 대한 포인터를 file_obj에 저장함

	if (file_obj == NULL) {	// 파일 객체가 성공적으로 열리지 않았다면,
		return -1;	// -1을 반환하여 실패를 나타냄
	}
	int fd = add_file_to_fd_table(file_obj); // 열린 파일 객체를 파일 디스크립터 테이블에 추가하고, 할당된 파일 디스크립터 번호를 fd에 저장함

	if (fd == -1) {	// 파일 디스크립터 번호 할당에 실패하면
		file_close(file_obj);	// 열린 파일을 닫음
	}
	return fd;	// 파일 디스크립터 번호를 반환함 (파일이 열린 경우, 파일 디스크립터를 실패한 경우, -1)
}

int add_file_to_fd_table(struct file *file) {
	struct thread *t = thread_current();	// 현재 실행 중인 쓰레드를 가져옴
	struct file **fdt = t->file_descriptor_table;	// 쓰레드의 파일 디스크립터 테이블 포인터를 가져옴
	int fd = t->fdidx;	// 파일 디스크립터의 시작 번호를 쓰레드의 fdidx에서 가져옴
	
	while (t->file_descriptor_table[fd] != NULL && fd < FDCOUNT_LIMIT) {	// 파일 디스크립터 테이블에서 빈 슬롯 찾기 루프
		fd++;
	}

	if (fd >= FDCOUNT_LIMIT) {	// 모든 슬롯이 사용 중이거나 FDCOUNT_LIMIT에 도달하면
		return -1;	// -1을 반환
	}

	t->fdidx = fd;	// 사용 가능한 파일 디스크립터 번호를 쓰레드의 fdidx에 저장
	fdt[fd] = file;	// 파일 객체를 파일 디스크립터 테이블의 해당 fd 위치에 저장
	return fd;	// 할당된 파일 디스크립터 번호를 반환함

}

/* 파일 디스크립터 번호(fd)를 통해 해당 쓰레드의 파일 디스크립터 테이블에서 파일 구조체의 포인터를 반환하는 함수 */
struct file *fd_to_struct_filep(int fd) {
	if (fd < 0 || fd >= FDCOUNT_LIMIT) {	// 파일 디스크립터 번호의 유효성을 검사
		return NULL;	// 유효하지 않으면 NULL을 반환
	}
	
	struct thread *t = thread_current();	// 현재 실행 중인 쓰레드의 포인터를 가져옴
	struct file **fdt = t->file_descriptor_table;	// 현재 쓰레드의 파일 디스크립터 테이블에 접근
	
	struct file *file = fdt[fd];	// 파일 디스크립터 번호에 해당하는 파일 구조체의 포인터를 가져옴
	return file;	// 가져온 파일 구조체 포인터를 반환함
}

// 파일의 크기를 반환하는 함수
int filesize(int fd) {
	struct file *fileobj = fd_to_struct_filep(fd);	// 파일 디스크립터 번호를 이용해 파일 구조체 포인터를 가져옴
	
	if (fileobj == NULL) {	// 파일 구조체 포인터가 유효하지 않다면(파일을 찾을 수 없으면),
		return -1;	// -1을 반환
	}

	return file_length(fileobj);	// 파일 구조체 포인터가 가리키는 파일의 크기를 반환함
}

// 열린 파일의 데이터를 읽어 버퍼에 넣음
int read(int fd, void *buffer, unsigned size) {
	// 버퍼 시작과 끝 주소의 유효성 검사
	// 버퍼의 시작 주소와 끝 주소가 사용자 메모리 영역 내에 있는지 확인
	check_address(buffer); 
	check_address(buffer + size -1); 

	// 버퍼를 가리키는 포인터
	// buffer의 시작 주소를 가리키는 새 포인터 buf 선언
	unsigned char *buf = buffer;
	int read_count;
	
	// 파일 디스크립터에서 해당하는 파일 객체 가져옴
	// fd_to_struct_fileep 함수를 사용해 주어진 파일 디스크립터에 해당하는 파일 객체를 가져옴
	struct file *fileobj = fd_to_struct_filep(fd);

	// 파일 객체가 없으면 -1 반환
	// 파일 객체가 없으면(파일 디스크립터가 유효하지 않으면) -1을 반환하여 오류 표시
	if (fileobj == NULL) {
		return -1;
	}

	// 표준 입력(STDIN)일 경우, 입력받은 데이터를 버퍼에 저장
	// 표준 입력(STDIN)에서 데이터를 읽어들여 buffer에 저장하는 과정
	// input_getc 함수를 사용해 사용자로부터 문자를 하나씩 받음
	if (fd == STDIN_FILENO) {
		char key;
		for (int read_count = 0; read_count < size; read_count++) {
			key  = input_getc();
			*buf++ = key;
			if (key == '\0') {
				break;
			}
		}
	}

	// 표준 출력(STDOUT)일 경우, -1 반환
	// 표준 출력(STDOUT)일 경우, -1을 반환하여 읽기 작업이 불가능함을 나타냄
	else if (fd == STDOUT_FILENO){
		return -1;
	}

	// 일반 파일일 경우, 파일 시스템 락을 사용하여 파일 읽기 수행
	// 일반 파일인 경우, 파일 시스템 락을 사용하여 동기화하고, file_read 함수로 파일로부터 데이터를 읽음
	else {
		lock_acquire(&filesys_lock);
		read_count = file_read(fileobj, buffer, size); // 파일 읽어들일 동안만 lock 걸어준다.
		lock_release(&filesys_lock);

	}
	// 읽은 바이트 수 반환
	return read_count;
}

// 지정된 파일 디스크립터가 가리키는 파일 내에서 주어진 위치로 커서를 이동하는 함수
void seek(int fd, unsigned position) {
	// 표준 입출력 파일 디스크립터 확인
	// 표준 입출력이 seek 작업을 지원하지 않기 때문에
	// 파일 디스크립터 fd가 표준 입력(0) 또는 출력(1)이면 함수를 종료함
	if (fd < 2) {
		return;
	}
	// 파일 디스크립터 범위 검증
	// fd가 유효한 범위 내에 있는지 확인하며 범위를 벗어나면 함수를 종료함
	if (fd < 0 || fd >= FDCOUNT_LIMIT) {
		return;
	}
	// 파일 객체 가져오기
	// fd에 해당하는 파일 객체를 가져옴
	struct file *file = fd_to_struct_filep(fd);
	// 주소 유효성 검사
	// 파일 객체의 주소가 유효한지 검사
	check_address(file);
	// 파일 객체 검증
	// 파일 객체가 NULL이면 함수 종료
	if (file == NULL) {
		return;
	}
	// 파일 내 위치 변경
	// 파일 내에서 지정된 position 위치로 이동
	file_seek(file, position);
}

// 주어진 파일 디스크립터(fd)가 가리키는 파일에서 현재 파일 포인터의 위치를 반환하는 함수
unsigned tell (int fd) {
	// 표준 입출력 파일 디스크립터 확인
	// fd가 표준 입력(0) 또는 출력(1)이면 함수를 종료함
	if (fd <2) {
		return;
	}
	// 파일 객체 가져오기
	// 파일 디스크립터에 해당하는 파일 객체를 가져옴
	struct file *file = fd_to_struct_filep(fd);
	// 주소 유효성 검사
	// 파일 객체의 주소가 유효한지 검사
	check_address(file);
	// 파일 객체 검증
	// 파일 객체가 NULL이면 함수를 종료
	if (file == NULL) {
		return;
	}
	// 파일 내 현재 위치 반환
	// 파일 내 현재 읽기/쓰기 위치를 반환함
	return file_tell(fd);
}

// 파일 디스크립터(fd)가 가리키는 파일을 닫음
void close (int fd) {
	// 표준 입출력 파일 디스크립터 확인
	// fd가 표준 입출력이면 함수를 종료함
	if (fd < 2) {
		return;
	}
	// 파일 객체 가져오기 및 검증
	// 파일 디스크립터에 해당하는 파일 객체를 가져오고 유효성을 검사함
	struct file *file = fd_to_struct_filep(fd);
	check_address(file);
	if (file == NULL) {
		return;
	// 파일 디스크립터 범위 검증
	// fd가 유효한 범위 내에 있는지 확인
	if (fd < 0 || fd >= FDCOUNT_LIMIT) {
		return;
	}
	// 파일 디스크립터 테이블에서 파일 제거
	// 현재 쓰레드의 파일 디스크립터 테이블에서 해당 파일 디스크립터를 NULL로 설정
	thread_current()->file_descriptor_table[fd] = NULL;
	}
}

// 주어진 파일 이름으로 새로운 프로세스를 실행함
int exec(char *file_name){
	// 주어진 file_name 주소가 유효한지 확인
	check_address(file_name);
	// file_name 문자열의 길이를 계산하고 널 종료 문자를 위해 1을 더함
	int size = strlen(file_name) + 1;
	// 페이지 할당 함수 palloc_get_page를 호출하여 새로운 페이지를 할당받고
	// 이 페이지를 fn_copy라는 새로운 문자열 포인터에 저장함
	char *fn_copy = palloc_get_page(PAL_ZERO);
	// fn_copy가 NULL이라면 메모리 할당이 실패한 것이므로
	// exit(-1)을 호출하여 프로그램을 비정상 종료시킴
	if ((fn_copy) == NULL) {
		exit(-1);
	}
	// file_name의 내용을 fn_copy로 복사함
	// size는 복사할 최대 문자 수를 제한
	strlcpy(fn_copy, file_name, size);
	// process_exec 함수를 호출하여 fn_copy에 저장된 프로그램을 실행
	// 이 함수가 -1을 반환하면 실행에 실패한 것으로 간주하고, -1을 반환
	if (process_exec(fn_copy) == -1) {
		return -1;
	}
	// 코드가 정상적으로 실행되지 않아야 하는 경로에 도달했음
	NOT_REACHED();
	return 0;
}

int wait (tid_t pid)
{
	process_wait(pid);
};

int process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */

	struct thread *cur = thread_current();
	struct thread *child = get_child_with_pid(child_tid);

	if (child == NULL)
		return -1;
	
	sema_down(&child->wait_sema); 
	int exit_status = child->exit_status;
	list_remove(&child->child_elem);
	sema_up(&child->free_sema);

	return exit_status;
}