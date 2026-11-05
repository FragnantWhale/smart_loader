#include "loader.h"
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int page_size = 4096;   
Elf32_Ehdr *ehdr;
Elf32_Phdr *phdr;
int fd;
void *file_buffer = NULL;

int page_faults = 0;
int page_allocs = 0;
int frag_bytes = 0;      

typedef struct page_node {
  int addr;                    
  struct page_node *next;
} page_node;
page_node *mapped_head = NULL;


void loader_cleanup() {
  while (mapped_head != NULL) {              
    page_node *n = mapped_head;
    munmap((void *)n->addr, page_size);
    mapped_head = n->next;
    free(n);
  }
  if (file_buffer) {
    free(file_buffer);
    file_buffer = NULL;
  }
  if (fd >= 0) {
    close(fd);
    fd = -1;
  }
}


Elf32_Phdr *find_segment(int addr) {
  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD &&
        addr >= phdr[i].p_vaddr &&
        addr < phdr[i].p_vaddr + phdr[i].p_memsz)
      return &phdr[i];
  }
  return NULL;
}


void segv_handler(int sig, siginfo_t *info, void *ctx) {
  page_faults++;
  int addr = (int)info->si_addr;

  Elf32_Phdr *seg = find_segment(addr);
  if (seg == NULL) {                       
    fprintf(stderr, "Invalid memory access at 0x%x\n", addr);
    loader_cleanup();
    exit(1);
  }

  int page_start = (addr / page_size) * page_size;   
  int page_end = page_start + page_size;

  void *p = mmap((void *)page_start, page_size,
                 PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);
  if (p == MAP_FAILED) {
    perror("mmap failed in page fault handler");
    loader_cleanup();
    exit(1);
  }

  page_node *node = malloc(sizeof(page_node));
  if (node == NULL) {
    perror("malloc failed in page fault handler");
    munmap(p, page_size);              
    loader_cleanup();
    exit(1);
  }
  node->addr = page_start;
  node->next = mapped_head;
  mapped_head = node;
  page_allocs++;


  int file_end = seg->p_vaddr + seg->p_filesz;
  
  int lo;
  if (page_start > seg->p_vaddr) {
    lo = page_start;
  } else {
    lo = seg->p_vaddr;
  }

  int hi;
  if (page_end < file_end) {
    hi = page_end;
  } else {
    hi = file_end;
  }
  
  if (lo < hi)
    memcpy((void *)lo,
           (char *)file_buffer + seg->p_offset + (lo - seg->p_vaddr),
           hi - lo);


  int seg_end = seg->p_vaddr + seg->p_memsz;
  if (page_start < seg->p_vaddr)
    frag_bytes += seg->p_vaddr - page_start;
  else if (seg_end < page_end)
    frag_bytes += page_end - seg_end;
}


int is_elf_manual(int fd) {
    char ident[16];
    if (read(fd, ident, 16) != 16) {
        return 0;
    }
    lseek(fd, 0, SEEK_SET);
    char magic[4] = {0x7f, 'E', 'L', 'F'};
    if (memcmp(ident, magic, 4) != 0) {
        return 0;
    } 
    if (ident[EI_CLASS] != ELFCLASS32) {
        return 0;
    }
    return 1;
}

void load_and_run_elf(char* exe) {
  fd = open(exe, O_RDONLY);
  if (fd < 0) {
    perror("Error opening executable file"); 
    exit(1);
  }

  if (!is_elf_manual(fd)) {
    fprintf(stderr, "Error: not a 32-bit ELF file.\n");
    loader_cleanup();
    exit(1);
  }
  struct stat filesize;
  if (fstat(fd, &filesize)<0){
    perror("Error finding size of the binary file"); 
    loader_cleanup();
    exit(1);
  }
  file_buffer = malloc(filesize.st_size);
  if (file_buffer==NULL){
    perror("Error allocating buffer to heap"); 
    loader_cleanup();
    exit(1);
  }
  int read_file = read(fd, file_buffer, filesize.st_size);
  if (read_file<0){
    perror("Error reading file");
    loader_cleanup();
    exit(1);
  }

  close(fd);
  fd=-1;

  ehdr=(Elf32_Ehdr *)file_buffer; 

  phdr=(Elf32_Phdr *)(file_buffer + ehdr->e_phoff); 


  if (find_segment(ehdr->e_entry) == NULL) {
    fprintf(stderr, "Error: entrypoint is not inside any PT_LOAD segment.\n");
    loader_cleanup();
    exit(1);
  }

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = segv_handler;
  sa.sa_flags = SA_SIGINFO;          
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGSEGV, &sa, NULL) < 0) {
    perror("sigaction failed");
    loader_cleanup();
    exit(1);
  }


  int (*_start)() = (int (*)())ehdr->e_entry;
  int result = _start();
  printf("User _start return value = %d\n", result);

  printf("Total page faults: %d\n", page_faults);
  printf("Total page allocations: %d\n", page_allocs);
  printf("Total internal fragmentation: %f KB\n", frag_bytes / 1024.0);
  loader_cleanup();
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <executable>\n", argv[0]);
        return 1;
    }
    
    load_and_run_elf(argv[1]); 
    
    return 0;
}
