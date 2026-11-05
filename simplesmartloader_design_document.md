# SimpleSmartLoader - Assignment 4 (Section-A)

**Group ID:** 160

## Group Members & Contributions
*   **[Satwik Biswas] (Roll No: [2024512]):** 
    *   *Contribution:* [100%]

**Private GitHub Repository:** [https://github.com/FragnantWhale]

---

## 1. Implementation Details

The `SimpleSmartLoader` acts as a custom execution environment for 32-bit ELF binaries. Unlike a standard loader that maps all required memory segments upfront, this loader implements **demand paging** (lazy loading).

### A. Initialization and Execution
1.  **ELF Verification:** The loader first verifies that the provided executable is a valid 32-bit ELF file by reading its magic numbers and class.
2.  **Buffer Loading:** The entire ELF file is loaded into a heap-allocated buffer to allow for quick access to segments later during page faults.
3.  **Bypassing Upfront Mapping:** Instead of mapping the `PT_LOAD` segments into memory, the loader simply extracts the `e_entry` (entry point) from the ELF header.
4.  **Signal Handler Registration:** A custom signal handler is registered for `SIGSEGV` using `sigaction` with the `SA_SIGINFO` flag, allowing the handler to access the exact faulting memory address.
5.  **Direct Jump:** The loader typecasts the entry point address to a function pointer and executes it. Because no memory has been mapped at that address, this intentionally triggers a segmentation fault.

### B. Demand Paging (SIGSEGV Handler)
When the program faults, the OS passes control to our `segv_handler`:
1.  **Segment Resolution:** The handler checks the faulting address (`info->si_addr`) against the ELF Program Headers (`PT_LOAD`) to verify the address is valid and belongs to an executable segment.
2.  **Page Alignment:** The loader calculates the 4KB (4096 bytes) page boundaries containing the faulting address. 
3.  **Memory Allocation:** Using `mmap` with `MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE`, the loader allocates exactly one 4KB page at the calculated page start address.
4.  **Selective Copying:** The loader calculates the exact byte boundaries of where the segment data overlaps with the newly allocated page. It then uses `memcpy` to load *only* that specific chunk of the segment from the file buffer into the mapped memory.
5.  **Resumption:** Once the handler returns, the OS re-attempts the instruction that caused the fault. Since the page is now loaded, the program resumes execution smoothly.

---

## 2. Memory Metrics & Tracking

As the program executes, the loader tracks three primary metrics to be displayed upon completion:

*   **Page Faults & Page Allocations:** Every time `segv_handler` is triggered for a valid memory segment, both the `page_faults` and `page_allocs` counters are incremented by 1. Because we allocate memory page-by-page, these values are identical.
*   **Internal Fragmentation:** Fragmentation is calculated strictly on a per-page basis. For every allocated page, the loader determines the exact start and end bytes actively used by the segment *within that specific page*. It then subtracts this used byte count from the 4096-byte page size. The resulting unused bytes are added to a global accumulator and finally displayed in Kilobytes (KB) to represent the wasted space (internal fragmentation) across the program's execution.

---

## 3. Resource Management & Cleanup

To prevent memory leaks, the loader implements a strict cleanup routine (`loader_cleanup`):
*   **Linked List Tracking:** Every time a new 4KB page is allocated via `mmap`, its address is stored in a dynamically allocated `page_node` structure, forming a linked list of all mapped pages.
*   **Tear Down:** Upon program completion (or fatal error), the cleanup function iterates through this linked list, calling `munmap` on every tracked 4KB page. It then frees the linked list nodes, frees the heap-allocated ELF file buffer, and closes any open file descriptors.
