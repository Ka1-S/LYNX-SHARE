// Round 190of: LD_PRELOAD helper for LYNX --pluginbench (Linux, glibc). Counts the malloc-family calls and the
// pthread mutex locks a thread makes while it has counting on; LYNX finds lynxbench_counts with dlsym. Build:
//   cc -O2 -shared -fPIC -o libbenchcount.so benchcount.c -ldl
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stddef.h>

extern void* __libc_malloc (size_t);
extern void* __libc_calloc (size_t, size_t);
extern void* __libc_realloc (void*, size_t);
extern void* __libc_memalign (size_t, size_t);
extern void __libc_free (void*);

static __thread int counting;
static __thread unsigned long allocations, locks;

void lynxbench_counts (int on, unsigned long* a, unsigned long* l)
{
    if (on)
    {
        allocations = locks = 0;
        counting = 1;
        return;
    }
    counting = 0;
    if (a) *a = allocations;
    if (l) *l = locks;
}

void* malloc (size_t n) { if (counting) ++allocations; return __libc_malloc (n); }
void* calloc (size_t a, size_t b) { if (counting) ++allocations; return __libc_calloc (a, b); }
void* realloc (void* p, size_t n) { if (counting) ++allocations; return __libc_realloc (p, n); }
void free (void* p) { if (counting && p) ++allocations; __libc_free (p); }
int posix_memalign (void** out, size_t align, size_t n)
{
    if (counting) ++allocations;
    void* p = __libc_memalign (align, n);
    if (! p) return 12;
    *out = p;
    return 0;
}
void* aligned_alloc (size_t align, size_t n) { if (counting) ++allocations; return __libc_memalign (align, n); }
void* memalign (size_t align, size_t n) { if (counting) ++allocations; return __libc_memalign (align, n); }

typedef int (*lockfn) (pthread_mutex_t*);
int pthread_mutex_lock (pthread_mutex_t* m)
{
    static lockfn real;
    if (! real)
        real = (lockfn) dlsym (RTLD_NEXT, "pthread_mutex_lock");
    if (counting) ++locks;
    return real (m);
}
