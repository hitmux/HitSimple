#include "OpenClDriver.h"

#include <array>
#include <cstring>

#if defined(__linux__)
#include <dlfcn.h>
#endif

namespace hitsimple::gpu::detail {

bool OpenClDriver::load(std::string& error) {
#if defined(__linux__)
  constexpr std::array<const char*, 2> libraries{"libOpenCL.so.1", "libOpenCL.so"};
  for (const auto* library : libraries) {
    handle_ = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (handle_ != nullptr) {
      break;
    }
  }
  if (handle_ == nullptr) {
    error = "OpenCL ICD loader library libOpenCL.so.1 is unavailable";
    return false;
  }
  return loadSymbol(getPlatformIds_, "clGetPlatformIDs", error) &&
         loadSymbol(getDeviceIds_, "clGetDeviceIDs", error) &&
         loadSymbol(getDeviceInfo_, "clGetDeviceInfo", error) &&
         loadSymbol(createContext_, "clCreateContext", error) &&
         loadSymbol(releaseContext_, "clReleaseContext", error) &&
         loadSymbol(createCommandQueue_, "clCreateCommandQueue", error) &&
         loadSymbol(releaseCommandQueue_, "clReleaseCommandQueue", error) &&
         loadSymbol(createBuffer_, "clCreateBuffer", error) &&
         loadSymbol(releaseMemObject_, "clReleaseMemObject", error) &&
         loadSymbol(createProgramWithSource_, "clCreateProgramWithSource", error) &&
         loadSymbol(buildProgram_, "clBuildProgram", error) &&
         loadSymbol(getProgramBuildInfo_, "clGetProgramBuildInfo", error) &&
         loadSymbol(releaseProgram_, "clReleaseProgram", error) &&
         loadSymbol(createKernel_, "clCreateKernel", error) &&
         loadSymbol(releaseKernel_, "clReleaseKernel", error) &&
         loadSymbol(setKernelArg_, "clSetKernelArg", error) &&
         loadSymbol(enqueueWriteBuffer_, "clEnqueueWriteBuffer", error) &&
         loadSymbol(enqueueReadBuffer_, "clEnqueueReadBuffer", error) &&
         loadSymbol(enqueueCopyBuffer_, "clEnqueueCopyBuffer", error) &&
         loadSymbol(enqueueNdRangeKernel_, "clEnqueueNDRangeKernel", error) &&
         loadSymbol(finish_, "clFinish", error);
#else
  error = "OpenCL dynamic loading is supported only on Linux";
  return false;
#endif
}

OpenClDriver::~OpenClDriver() {
#if defined(__linux__)
  if (handle_ != nullptr) {
    dlclose(handle_);
  }
#endif
}

template <typename Function>
bool OpenClDriver::loadSymbol(Function& target, const char* name, std::string& error) {
#if defined(__linux__)
  const auto symbol = dlsym(handle_, name);
  if (symbol == nullptr) {
    error = std::string("OpenCL ICD loader does not export ") + name;
    return false;
  }
  static_assert(sizeof(target) == sizeof(symbol));
  std::memcpy(&target, &symbol, sizeof(target));
  return true;
#else
  static_cast<void>(target);
  static_cast<void>(name);
  error = "OpenCL dynamic loading is supported only on Linux";
  return false;
#endif
}

} // namespace hitsimple::gpu::detail
