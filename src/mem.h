#pragma once

#include <stddef.h>
//#include <utility> // std::move

// operator new() without #include <new>
// Unfortunately the standard mandates the use of size_t, so we need stddef.h the very least.
// Trick via https://github.com/ocornut/imgui
// "Defining a custom placement new() with a dummy parameter allows us to bypass including <new>
// which on some platforms complains when user has disabled exceptions."
struct _X__NewDummy {};
inline void* operator new(size_t, _X__NewDummy, void* ptr) { return ptr; }
inline void  operator delete(void*, _X__NewDummy, void*) {}
#define _X_PLACEMENT_NEW(p) new(_X__NewDummy(), p)

// Helpers for memory range manipulation
template<typename T>
void mem_destruct(T* begin, T* end)
{
    for (T* p = begin; p < end; ++p)
        p->~T();
}

template<typename T>
void mem_construct_default(T* begin, T* end)
{
    for (T* p = begin; p < end; ++p)
        _X_PLACEMENT_NEW(p) T();
}

template<typename T>
void mem_construct_from(T* begin, T* end, const T& x)
{
    for (T* p = begin; p < end; ++p)
        _X_PLACEMENT_NEW(p) T(x);
}

// not needed for now
/*template<typename T>
void mem_construct_move_from(T* begin, T* end, const T *movebegin)
{
    for (T* p = begin; p < end; ++p)
        _X_PLACEMENT_NEW(p) T(std::move(*movebegin++));
}*/
