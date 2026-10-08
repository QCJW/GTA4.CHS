#pragma once
#include "../common/stdinc.h"

//在没有定义的情况下，手动访问结构体成员
class class_pointer
{
public:
    template <typename T>
    T* get_field(std::uintptr_t offset) const
    {
        return reinterpret_cast<T*>(reinterpret_cast<std::intptr_t> (this) + offset);
    }

};
