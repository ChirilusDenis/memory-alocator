// SPDX-License-Identifier: BSD-3-Clause

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <errno.h>

#include "osmem.h"
#include "usefull.h"

block_meta * head = NULL;
int prev_prealloc;

void *os_malloc(size_t size)
{
	if (size <= 0)
		return NULL;
	int stat = 0;

	void *allcd = NULL;

	size_t actual_size = align_8(METADATA_SIZE) + align_8(size);

	if (actual_size <= MMAP_THRESOLD) {
		allcd = req_space(head, size);
		if (allcd) {
			((block_meta *)allcd)->status = STATUS_ALLOC;
			return ((char *)allcd) +  align_8(METADATA_SIZE);
		}

		allcd = is_last_free(head);
		if (allcd) {
			sbrk(align_8(size) - align_8(((block_meta *)allcd)->size));
			((block_meta *)allcd)->size = size;
			((block_meta *)allcd)->status = STATUS_ALLOC;
			return ((char *)allcd) +  align_8(METADATA_SIZE);
		}

		if (prev_prealloc == 0) {
			allcd = sbrk(0x20000);
			prev_prealloc = 1;
		} else {
			allcd = sbrk(actual_size);
		}
		stat = STATUS_ALLOC;
	} else {
		allcd = mmap(NULL, actual_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		stat = STATUS_MAPPED;
		}

	if ((long)allcd <= 0) {
		errno = -(long)allcd;
		return NULL;
	}

		((block_meta *)allcd)->size = size;
		((block_meta *)allcd)->status = stat;
		((block_meta *)allcd)->prev = NULL;
		((block_meta *)allcd)->next = NULL;

		if (head == NULL) {
			head = allcd;
		} else {
			block_meta *curr = head;

			while (curr->next)
				curr = curr->next;
			curr->next = allcd;
			((block_meta *)allcd)->prev = curr;
		}

		return ((char *)allcd) + align_8(METADATA_SIZE);
}

void os_free(void *ptr)
{
	if (ptr) {
		block_meta *start = (block_meta *)((char *)ptr - align_8(METADATA_SIZE));

		if (start->status == STATUS_ALLOC) {
			start->status = STATUS_FREE;

			combine_all(head);
		}

		if (start->status == STATUS_MAPPED) {
			if (start->next == start->prev)
				head = NULL;
			if (start->prev)
				start->prev->next = start->next;
			else
				head = start->next;
			if (start->next)
				start->next->prev = start->prev;

			int ret = munmap(start, align_8(METADATA_SIZE) + align_8(start->size));

			if (ret < 0)
				errno = -ret;
		}
	}
}

void *os_calloc(size_t nmemb, size_t size)
{
	if (size <= 0 || nmemb <= 0)
		return NULL;
	int stat = 0;

	void *allcd = NULL;

	size_t actual_size = align_8(METADATA_SIZE) + align_8(size * nmemb);

	if (actual_size <= (unsigned int)getpagesize()) {
		allcd = req_space(head, size*nmemb);
		if (allcd) {
			((block_meta *)allcd)->status = STATUS_ALLOC;
			memset(((char *)allcd) +  align_8(METADATA_SIZE), 0, size*nmemb);
			return ((char *)allcd) +  align_8(METADATA_SIZE);
		}

		allcd = is_last_free(head);
		if (allcd) {
			sbrk(align_8(size*nmemb) - align_8(((block_meta *)allcd)->size));
			((block_meta *)allcd)->size = size*nmemb;
			((block_meta *)allcd)->status = STATUS_ALLOC;
			memset(((char *)allcd) +  align_8(METADATA_SIZE), 0, size*nmemb);
			return ((char *)allcd) +  align_8(METADATA_SIZE);
		}

		if (head == NULL)
			allcd = sbrk(0x20000);
		else
			allcd = sbrk(actual_size);

		stat = STATUS_ALLOC;
	} else {
		allcd = mmap(NULL, actual_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		stat = STATUS_MAPPED;
		}

	if ((long)allcd <= 0) {
		errno = -(long)allcd;
		return NULL;
	}

		((block_meta *)allcd)->size = size*nmemb;
		((block_meta *)allcd)->status = stat;
		((block_meta *)allcd)->prev = NULL;
		((block_meta *)allcd)->next = NULL;

		if (head == NULL) {
			head = allcd;
		} else {
			block_meta *curr = head;

			while (curr->next)
				curr = curr->next;
			curr->next = allcd;
			((block_meta *)allcd)->prev = curr;
		}
		memset(((char *)allcd) +  align_8(METADATA_SIZE), 0, size*nmemb);
		return ((char *)allcd) + align_8(METADATA_SIZE);
}

void *os_realloc(void *ptr, size_t size)
{
	if (!ptr)
		return os_malloc(size);
	if (!size) {
		os_free(ptr);
		return NULL;
	}

	block_meta *start = (block_meta *)(((char *)ptr) - align_8(METADATA_SIZE));
	size_t old_size = start->size;

	if (start->status == STATUS_FREE)
		return NULL;

	if (start->status == STATUS_ALLOC) {
		if (align_8(size) <= align_8(start->size)) {
			if (align_8(start->size) > (align_8(METADATA_SIZE) + align_8(size)))
				split(start, size);
			return ptr;
		}

		if (is_last(start)) {
			sbrk(align_8(size) - align_8(start->size));
			start->size = size;
			return ptr;
		}

		block_meta *new = is_next_free(start);

		if (new) {
			while (new) {
				combine(start, new);
				new = is_next_free(start);
			}
			if (align_8(start->size) >= align_8(size)) {
				if (align_8(start->size) > (align_8(METADATA_SIZE) + align_8(size)))
					split(start, size);
				return ptr;
			}
		}
	}

	void *new_ptr = os_malloc(size);

	memcpy(new_ptr, ptr, get_min(old_size, size));
	os_free(ptr);

	return new_ptr;
}
