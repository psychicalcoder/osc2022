#include <string.h>
#include <stddef.h>
#include "mem.h"
#include "textio.h"
#include "devtree.h"

#define FREE_BODY -1
#define ALLOCATED -3

#define MAX_EXP 7
// 2^0 ~ 2^5

#define FRAME_IDX(frame_list_node) (frame_list_node-frame_list)
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

extern int heap_begin; // define in the linker scripts
uint64_t memory_size;

struct FrameList {
  struct FrameList *next;
  struct FrameList *prev;
};

struct FrameList *frame_list;

int *frame_array;
struct FrameList **free_list;
struct FrameList **last_elm;

void* smalloc(uint32_t size) {
  static uint64_t offset;
  void* ptr = (void*)((&heap_begin) + offset);
  offset += size;
  for (int i = 0; i < size; i++) {
    ((char*)ptr)[i] = 0;
  }
  return ptr;
}

void append_frame(struct FrameList *fp, int exp) {
  if (free_list[exp] == NULL) {
    free_list[exp] = fp;
    last_elm[exp] = fp;
    fp->prev = NULL;
    fp->next = NULL;
  } else {
    fp->prev = last_elm[exp];
    fp->next = NULL;
    last_elm[exp]->next = fp;
    last_elm[exp] = fp;
  }
}

void remove_frame(struct FrameList *fp, int exp) {
  
  if (fp->prev != NULL) {
    fp->prev->next = fp->next;
  }

  if (fp->next != NULL) {
    fp->next->prev = fp->prev;
  }


  if (free_list[exp] == fp) {
    free_list[exp] = fp->next;
  }
  if (last_elm[exp] == fp) {
    last_elm[exp] = last_elm[exp]->prev;
  }
  
  fp->prev = NULL;
  fp->next = NULL;
}

unsigned long long align_addr(unsigned long long addr)
{
  if (addr % FRAME_SIZE) {
    return (addr / FRAME_SIZE + 1) * FRAME_SIZE;
  } else {
    return addr;
  }
}


void getmemory_size(const char *node_name, const char *prop_name, void *value, uint32_t size) {
  if (strncmp(node_name, "memory@0", 255) == 0) {
    if (strncmp(prop_name, "reg", 255) == 0) {
      memory_size = fdt32_to_cpu(((uint32_t*)value)[1]);
    }
  }
}

void init_frame_allocator() {
  const int conti_size = (1<<MAX_EXP);
  int i = 0;

  fdt_traverse(getmemory_size);

  kprintf("[KMem] Memory Size: %lx bytes\n", memory_size);
  
  frame_list = smalloc((memory_size / FRAME_SIZE) * sizeof(struct FrameList));
  frame_array = smalloc((memory_size / FRAME_SIZE) * sizeof(int));
  free_list = smalloc((MAX_EXP + 1) * sizeof(struct FrameList*));
  last_elm = smalloc((MAX_EXP + 1) * sizeof(struct FrameList*));
  
  memset(frame_array, -1, (memory_size / FRAME_SIZE) * sizeof(struct FrameList));
  for (i = 0; i < (memory_size / FRAME_SIZE); i += conti_size) {
    frame_array[i] = MAX_EXP;
    append_frame(&frame_list[i], MAX_EXP);
  }

  uint64_t align_allocator_start = (uint64_t)frame_list / FRAME_SIZE * FRAME_SIZE;
  uint64_t allocator_end = (uint64_t)last_elm + (MAX_EXP + 1) * sizeof(struct FrameList*);
  uint64_t align_allocator_end = align_addr(allocator_end);
  memory_reserve(align_allocator_start / FRAME_SIZE, (align_allocator_end - align_allocator_start) / FRAME_SIZE);
  kprintf("[KMem] Frame Allocator Initialized\n");
}

/* this function return the index of the allocated frame */
int allocate_frame(int reqexp) {
  if (reqexp > MAX_EXP || reqexp < 0) {
    return -1;
  }

  if (free_list[reqexp] == NULL) {
    // request from a larger block;
    int upper_exp = -1;
    for (int i = reqexp+1; i <= MAX_EXP; i++) {
      if (free_list[i] != NULL) {
        upper_exp = i;
        break;
      }
    }
    
    if (upper_exp < 0) {
      return -1;
    }

    while (upper_exp > reqexp) {
      struct FrameList *ff = free_list[upper_exp];
      remove_frame(ff, upper_exp);
      upper_exp--;
      struct FrameList *ff2 = ff + (1<<upper_exp);
      frame_array[FRAME_IDX(ff)] = upper_exp;
      frame_array[FRAME_IDX(ff2)] = upper_exp;
      append_frame(ff, upper_exp);
      append_frame(ff2, upper_exp);
    }
  }
  
  struct FrameList* free_frame = free_list[reqexp];
  remove_frame(free_frame, reqexp);
  int idx = FRAME_IDX(free_frame);

  for (int i = 0; i < (1<<reqexp); i++) {
    frame_array[idx+i] = ALLOCATED;
  }

  kprintf("[KMem] Page No.%d (size: 0x%x) is allocated\n", idx, (1<<reqexp)*FRAME_SIZE); 
  return idx;
}


int deallocate_frame(int idx) {
  if (frame_array[idx] != ALLOCATED) return -1;

  kprintf("[KMem] Deallocate page: %d\n", idx);
  
  frame_array[idx] = 0;
  append_frame(&frame_list[idx], 0);
  // merge free block with body
  for (int i = 0; i < MAX_EXP; i++) {
    int buddy_idx = idx ^ (1<<i);
    if (frame_array[buddy_idx] == i) {
      kprintf("[K] Merge frame %d and %d\n", idx, buddy_idx);
      remove_frame(&frame_list[buddy_idx], i);
      remove_frame(&frame_list[idx], i);
      if (buddy_idx < idx) {
        int tmp = idx;
        idx = buddy_idx;
        buddy_idx = tmp;
      }
      frame_array[idx] = i+1;
      frame_array[buddy_idx] = FREE_BODY;
      append_frame(&frame_list[idx], i+1);
    } else {
      break;
    }
  }
  return 0;
}

int deallocate_cont_frame(int idx, int num) {
  for (int i = 0; i < num; i++)
    deallocate_frame(idx+i);
  return 0;
}


void reserve_oversize_frame(struct FrameList *frame, int exp, int l, int r) {
  int frame_idx = FRAME_IDX(frame);
  int frame_idx2;
  struct FrameList *frame2;
  if (l >= r) return;

  // remove from the free list
  remove_frame(frame, exp);

  if (frame_idx == l && (frame_idx + (1<<exp)) == r) {
    // match the whole interval
    for (int i = frame_idx; i < r; i++)
      frame_array[i] = ALLOCATED;
    return;
  }

  frame_idx2  = frame_idx + (1<<(exp-1));
  frame2 = &frame_list[frame_idx2];
  
  // split the large conti-frame
  frame_array[frame_idx] = exp-1;
  frame_array[frame_idx2] = exp-1;
  append_frame(&frame_list[frame_idx], exp-1);
  append_frame(&frame_list[frame_idx2], exp-1);

  // divide the interval into the two half
  if (frame_idx2 >= r) {
    reserve_oversize_frame(&frame_list[frame_idx], exp-1, l, r);
  } else {
    reserve_oversize_frame(&frame_list[frame_idx], exp-1, l, frame_idx2);
    reserve_oversize_frame(&frame_list[frame_idx2], exp-1, frame_idx2, r);
  }
}

void memory_reserve(int start_idx, int count) {
  // kprintf("[KMem] reserve from %d to %d\n", start_idx, count);
  int head_idx = start_idx;
  int exp = 0;
  while (exp <= MAX_EXP) {
    if (frame_array[head_idx] >= 0) {
      break;
    }
    if (head_idx & (1<<exp)) head_idx ^= (1<<exp);
    exp++;
  }
  if (exp > MAX_EXP) return;
  exp = frame_array[head_idx];

  if (head_idx + (1<<exp) >= start_idx + count) {
    reserve_oversize_frame(&frame_list[head_idx], exp, start_idx, start_idx + count);
  } else {
    int usable_size = start_idx - head_idx;
    reserve_oversize_frame(&frame_list[head_idx], exp, start_idx, head_idx + (1<<exp));
    memory_reserve(head_idx+(1<<exp), count - (1<<exp) + usable_size);
  }
}

void* getFreePage() {
  int idx = allocate_frame(0);
  return (void*)(MEMORY_BASE + idx * FRAME_SIZE);
}

void* getContFreePage(int cnt, int *exp) {
  if (cnt > (1<<MAX_EXP)) return NULL;
  int celi;
  for (celi = 0; celi <= MAX_EXP; celi++) {
    if ((1<<celi) >= cnt) break;
  }
  int idx = allocate_frame(celi);
  *exp = celi;
  return (void*)(MEMORY_BASE + idx * FRAME_SIZE);
}

void returnPage(void *ptr) {
  deallocate_frame(((uint64_t)ptr - MEMORY_BASE) / FRAME_SIZE);
}