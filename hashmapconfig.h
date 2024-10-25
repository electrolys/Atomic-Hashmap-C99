//Setup all the needed functions and include your preferred Atomics/Spinlock library
#include <SDL2/SDL_atomic.h>
#include <stdlib.h>

#define HSHM_CASPTR(var,old,new) SDL_AtomicCASPtr((void**)var,(void*)old,(void*)new)
#define HSHM_SWAPPTR(var,x) SDL_AtomicSetPtr((void**)var,(void*)x)
#define HSHM_GETPTR(var) SDL_AtomicGetPtr((void**)var)

#define HSHM_ATOMIC_T SDL_atomic_t
#define HSHM_ANULL(var) (var) = {0}
#define HSHM_AADD(var,x) SDL_AtomicAdd(var,x)
#define HSHM_ASWAP(var,x) SDL_AtomicSet(var,x)
#define HSHM_AGET(var) SDL_AtomicGet(var)

//lock has to be stack allocated
#define HSHM_LOCK_T SDL_SpinLock
#define HSHM_LNULL(var) (var) = 0
#define HSHM_LOCK(lock) SDL_AtomicLock(lock)
#define HSHM_TRYLOCK(lock) SDL_AtomicTrylock(lock) // must return non zero on success
#define HSHM_UNLOCK(lock) SDL_AtomicUnlock(lock)

#define HSHM_MEMCMP memcmp
#define HSHM_MEMCPY memcpy
