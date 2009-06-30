#include <malloc.h>
#include <syscall.h>
#include <limits.h>

#include "omx_lib.h"

#warning ah ha

static void * omx___mremap(void *, size_t, size_t, int, void*) __asm__ ("__mremap");

static void *
omx___mremap(void *old_ptr, size_t old_size, size_t new_size, int maymove, void * extra)
{
  omx__regcache_clean(old_ptr, old_size);
  return (void *) syscall(__NR_mremap, old_ptr, old_size, new_size, maymove, extra);
}

static void * omx__mremap(void *,size_t,size_t,int, void*) __asm__ ("mremap");

static void *
omx__mremap(void *old_ptr, size_t old_size, size_t new_size, int maymove, void *extra)
{
  return omx___mremap(old_ptr, old_size, new_size, maymove, extra);
}

static int omx__rcache_hook_triggered;

static int
__munmap(void *old_ptr, size_t old_size)
{
  omx__rcache_hook_triggered = 1;
  omx__regcache_clean(old_ptr, old_size);
  return syscall(__NR_munmap, old_ptr, old_size);
}

static int
munmap(void *old_ptr, size_t old_size)
{
  return __munmap(old_ptr, old_size);
}

extern void *__sbrk(intptr_t inc);

char * omx__init_brk;

void *
sbrk(intptr_t inc)
{
  void *ptr;

  omx__rcache_hook_triggered = 1;
  ptr = __sbrk(inc);
  if (ptr && (intptr_t) ptr != -1 && inc)
    omx__regcache_clean((inc >= 0 ? ptr : (char*)ptr - inc), inc);
  return ptr;
}

static void *
omx__morecore(intptr_t inc)
{
  void *res = __sbrk(inc);
  return (intptr_t)res == -1 ? 0 : res;
}

extern int _DYNAMIC __attribute__((weak));

void dlfree(void *);
void *dlmalloc(size_t);
void *dlmemalign(size_t,size_t);
void *dlrealloc(void *,size_t);

static void
free_hook(void *ptr, const void *caller)
{
  return dlfree(ptr);
}

static void *
malloc_hook(size_t len, const void *caller)
{
  return dlmalloc(len);
}

static void *
memalign_hook(size_t boundary,size_t size, const void * caller)
{
  return dlmemalign(boundary,size);
}

static void *
realloc_hook(void *ptr,size_t len, const void * caller)
{
  return dlrealloc(ptr,len);
}

//static
void
omx_regcache_hook(void)
{
  int opt_rcache = omx__globals.regcache;
  /* regcache does not work with static libc */
  if (&_DYNAMIC == 0)
    opt_rcache = 0;
  else if (opt_rcache == 1)
    opt_rcache = mx__regcache_works();

  omx__verbose_printf(NULL, "regcache is %s\n", opt_rcache ? "enabled" : "disabled");
  if (opt_rcache) {
    __free_hook = free_hook;
    __malloc_hook = malloc_hook;
    __memalign_hook = memalign_hook;
    __realloc_hook = realloc_hook;
  }
}

void (*__malloc_initialize_hook)() = omx_regcache_hook;

static int mx__hook_triggered;

int mx__regcache_works()
{
  static int works_ok = 0;
  static int already_called;
  if (already_called)
    return works_ok;
  already_called = 1;
  mx__hook_triggered = 0;
  free(malloc(1));
  free(malloc(4*1024*1024));
  /* catching address space changes works if
      - static linking (libc/malloc uses our __munmap/__morecore)
      - malloc != libc_malloc (detected by __malloc_hook != 0)
  */
  if ((&_DYNAMIC == 0 || __malloc_hook != 0) && mx__hook_triggered) {
    //    __morecore = mx__morecore;
    works_ok = 1;
    return 1;
  }
  works_ok = 0;
  return works_ok;
}
