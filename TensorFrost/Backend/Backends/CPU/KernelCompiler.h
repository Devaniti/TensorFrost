#pragma once

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#define NOMINMAX
#include <windows.h>

#include "Backend/Backends/CPU/Memory.h"
#include "Backend/CodeGen/Generators.h"
#include "Backend/KernelExecutor.h"
#include "Backend/TensorMemory.h"
#include "IR/KernelGen.h"

namespace TensorFrost {

using namespace std;

extern std::string kernel_compile_options;

void CompileAndLoadKernel(Program* program);

}  // namespace TensorFrost