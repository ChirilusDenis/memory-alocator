#define MMAP_THRESOLD (128 * 1024)
#define STATUS_FREE   0
#define STATUS_ALLOC  1
#define STATUS_MAPPED 2

typedef struct block_meta {
    size_t size;
    int status;
    struct block_meta *prev;
    struct block_meta *next;
}block_meta;

#define METADATA_SIZE (sizeof(block_meta))

size_t align_8(size_t size) {
    if (size % 8 == 0) return size;
    return size + 8 - (size % 8);
}

void split(block_meta *first, size_t size) {
    size_t first_size = align_8(METADATA_SIZE) + align_8(size);

    block_meta *second = (block_meta *)(((char *)first) + first_size);
    second->status = STATUS_FREE;
    second->size = align_8(first->size) - first_size;
    first->size = size;

    if (first->next) {
        first->next->prev = second;
    }
    second->next = first->next;
    first->next = second;
    second->prev = first;
}

block_meta *req_space(block_meta *start, size_t size) {
    block_meta *bst = NULL;
    size_t min_size = align_8(size) + align_8(METADATA_SIZE);

    while (start) {
        if (start->status == STATUS_FREE) {
            if (align_8(start->size) >= align_8(size)) {
                if (!bst || ((start->size - size) < (bst->size - size))) {
                    bst = start;
                }
            }
        }
        start = start->next;
    }

    if (bst && (align_8(bst->size) > min_size)) {
        split(bst, size);
    }

    return bst;
}

void combine(block_meta *first, block_meta *second) {
    first->size = align_8(first->size) + align_8(second->size) + align_8(METADATA_SIZE);
    if (second->next) {
        second->next->prev = second->prev;
    }
    second->prev->next = second->next;
}

void combine_all(block_meta *head) {
    block_meta *tmp;
    if (head) {
        while (head && head->next) {
            if (head->status == STATUS_FREE) {
                tmp = head->next;
                while (tmp && tmp->status != STATUS_ALLOC) {
                    if (tmp->status == STATUS_FREE) {
                        combine(head, tmp);
                    }
                    tmp = tmp->next;
                }
            }
            head = head->next;
        }
    }
}

block_meta *is_last_free(block_meta *head) {
    block_meta *free_block = NULL;
    while (head) {
        if (head->status == STATUS_ALLOC) {
            free_block = NULL;
        }
        if (head->status == STATUS_FREE) {
            free_block = head;
        }
        head = head->next;
    }
    return free_block;
}

block_meta *is_next_free(block_meta *head) {
    if (!head) {
        return NULL;
    }
    head = head->next;
    while (head) {
        if (head->status == STATUS_ALLOC) {
            return NULL;
        }
        if (head->status == STATUS_FREE) {
            return head;
        }
        head = head->next;
    }
    return NULL;
}

int is_last(block_meta *head) {
    if (head->next) {
        head = head->next;
    } else {
        return 1;
    }
    while (head) {
        if (head->status == STATUS_ALLOC || head->status == STATUS_FREE) {
            return 0;
        }
        head = head->next;
    }
    return 1;
}

size_t get_min(size_t a, size_t b) {
    if (a < b) {
        return a;
    }
    return b;
}
