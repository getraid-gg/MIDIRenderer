#pragma once
#include <memory>

template<typename T>
using deleter_t = void(*)(T*);

template<typename T>
using deleter_unique_ptr = std::unique_ptr<T, deleter_t<T>>;
