// The Vulkan Memory Allocator is a single header library: every file
// includes vk_mem_alloc.h for the declarations, and exactly one file defines
// VMA_IMPLEMENTATION to compile the implementation. This is that file.
//
// volk loads the Vulkan functions at runtime, so VMA must not expect them to
// be linked in (static) but fetch them itself (dynamic), through the two
// functions the device hands it.
#include <volk.h>

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>
