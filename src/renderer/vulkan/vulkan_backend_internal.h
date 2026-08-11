#pragma once

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_CONSTRUCTORS

#include "renderer/vulkan/vulkan_backend.h"
#include "utility/logger.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
