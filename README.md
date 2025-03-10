# Memory Allocator

## Intruduction

This project implments minimal versions of `malloc()`, `calloc()`, `realloc()`, and `free()` using system calls.

## API

1. `void *os_malloc(size_t size)`

   Allocates `size` bytes and returns a pointer to the allocated memory.

   Chunks of memory smaller than `MMAP_THRESHOLD` are allocated with `brk()`.
   Bigger chunks are allocated using `mmap()`.
   The memory is uninitialized.

   - Passing `0` as `size` will return `NULL`.

1. `void *os_calloc(size_t nmemb, size_t size)`

   Allocates memory for an array of `nmemb` elements of `size` bytes each and returns a pointer to the allocated memory.

   Chunks of memory smaller than [`page_size`] are allocated with `brk()`.
   Bigger chunks are allocated using `mmap()`.
   The memory is set to zero.

   - Passing `0` as `nmemb` or `size` will return `NULL`.

1. `void *os_realloc(void *ptr, size_t size)`

   Changes the size of the memory block pointed to by `ptr` to `size` bytes.
   If the size is smaller than the previously allocated size, the memory block will be truncated.

   If `ptr` points to a block on heap, `os_realloc()` will first try to expand the block, rather than moving it.
   Otherwise, the block will be reallocated and its contents copied.

   When attempting to expand a block followed by multiple free blocks, `os_realloc()` will coalesce them one at a time and verify the condition for each.
   Blocks will remain coalesced even if the resulting block will not be big enough for the new size.

   Calling `os_realloc()` on a block that has `STATUS_FREE` will return `NULL`.
   This is a measure to prevent undefined behavior and make the implementation robust, it should not be considered a valid use case of `os_realloc()`.

   - Passing `NULL` as `ptr` will have the same effect as `os_malloc(size)`.
   - Passing `0` as `size` will have the same effect as `os_free(ptr)`.

1. `void os_free(void *ptr)`

   Frees memory previously allocated by `os_malloc()`, `os_calloc()` or `os_realloc()`.

   `os_free()` will not return memory from the heap to the OS by calling `brk()`, but rather mark it as free and reuse it in future allocations.
   In the case of mapped memory blocks, `os_free()` will call `munmap()`.

1. General

   - Allocations that increase the heap size will only expand the last block if it is free.

## Implementation

This is relatively efficient implementation that keeps data aligned, keeps track of memory blocks and reuse freed blocks.
This can be further improved by reducing the number of syscalls and block operations.

### [Memory Alignment]

All allocated memory is be aligned (i.e. all addresses are multiple of a given size).
This is a space-time trade-off because memory blocks are padded so each can be read in one transaction.
It also allows for atomicity when interacting with a block of memory.

All memory allocations are aligned to **8 bytes** as required by 64 bit systems.

### Block Reuse

#### `struct block_meta`

The structure `block_meta` will be used to manage the metadata of a block.
Each allocated zone will comprise of a `block_meta` structure placed at the start, followed by data (**payload**).
For all functions, the returned address will be that of the **payload** (not of the `block_meta` structure).

```C
struct block_meta {
	size_t size;
	int status;
	struct block_meta *prev;
	struct block_meta *next;
};
```

_Note_: Both the `struct block_meta` and the **payload** of a block are aligned to **8 bytes**.


#### Split Block

Reusing memory blocks improves the allocator's performance, but might lead to [Internal Memory Fragmentation]
This happens when we allocate a size smaller than all available free blocks.
If we use one larger block the remaining size of that block will be wasted since it cannot be used for another allocation.

To avoid this, a block is be truncated to the required size and the remaining bytes will be used to create a new free block.

All the resulting free block are reusable.
The split is not performed if the remaining size (after reserving space for `block_meta` structure and payload) is not big enough to fit another block (`block_meta` structure and at least **1 byte** of usable memory).

#### Coalesce Blocks

There are cases when there is enough free memory for an allocation, but it is spread across multiple blocks that cannot be used.
This is called [External Memory Fragmentation].

One technique to reduce external memory fragmentation is **block coalescing** which implies merging adjacent free blocks to form a contiguous chunk.

Coalescing is used before searching for a block and in `os_realloc()` to expand the current block when possible.

_Note_: Blocks can still be split after a coalesce

#### Find Best Block

On every allocation we search the whole list of blocks and choose the best fitting free block, to reduce future operations on it.

_Note_: For consistent results, all adjacent free blocks are coalesced before searching.

### Heap Preallocation

Heap is used in most modern programs.
This hints at the possibility of preallocating a relatively big chunk of memory (i.e. **128 kilobytes**) when the heap is used for the first time.
This reduces the number of future `brk()` syscalls.

_Note_: Heap preallocation happens only once.

## Building Memory Allocator

To build `libosmem.so`, run `make` in the `src/` directory:

```console
student@os:~/.../mem-alloc$ cd src/
student@os:~/.../mem-alloc/src$ make
gcc -fPIC -Wall -Wextra -g -I../utils  -c -o osmem.o osmem.c
gcc -fPIC -Wall -Wextra -g -I../utils  -c -o ../utils/printf.o ../utils/printf.c
gcc -shared -o libosmem.so osmem.o helpers.o ../utils/printf.o
```

## Testing and Grading

Testing is automated.
Tests are located in the `tests/` directory:

```console
student@so:~/.../mem-alloc/tests$ ls -F
Makefile  grade.sh@  ref/  run_tests.py  snippets/
```

To test and grade your assignment solution, enter the `tests/` directory and run `grade.sh`.
Note that this requires linters being available.
When using `grade.sh` you will get grades for correctness (maximum `90` points) and for coding style (maximum `10` points).
A successful run will provide you an output ending with:

```console
### GRADE


Checker:                                                         90/ 90
Style:                                                           10/ 10
Total:                                                          100/100


### STYLE SUMMARY


```

### Running the Checker

To run only the checker, use the `run_tests.py` script from the `tests/` directory.

Before running `run_tests.py`, you first have to build `libosmem.so` in the `src/` directory and generate the test binaries in `tests/snippets`.
You can do so using the all-in-one `Makefile` rule from `tests/`: `make check`.

**NOTE:** By default, `run_tests.py` checks for memory leaks, which can be time-consuming.
To speed up testing, use the `-d` flag or `make check-fast` to skip memory leak checks.

### Running the Linters

To run the linters, use the `make lint` command in the `tests/` directory.
Note that the linters have to be installed on your system: [`checkpatch.pl`](https://.com/torvalds/linux/blob/master/scripts/checkpatch.pl), [`cpplint`](https://github.com/cpplint/cpplint), [`shellcheck`](https://www.shellcheck.net/) with certain configuration options.
It's easiest to run them in a Docker-based setup with everything configured:

```console
student@so:~/.../mem-alloc/tests$ make lint
[...]
cd .. && checkpatch.pl -f checker/*.sh tests/*.sh
[...]
cd .. && cpplint --recursive src/ tests/ checker/
[...]
cd .. && shellcheck checker/*.sh tests/*.sh
```
