#pragma once
#include <stdint.h>
#include <stddef.h>
namespace wsprd {

uint32_t nhash(const void* key, size_t length, uint32_t initval);

} // namespace wsprd
