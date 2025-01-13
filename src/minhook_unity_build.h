#pragma once

#include "../betterapi.h"

// use minhook to hook old_func and redirect to new_func, return a pointer to call old_func
// returns null on error
BC_EXPORT FUNC_PTR minhook_hook_function(FUNC_PTR old_func, FUNC_PTR new_func);