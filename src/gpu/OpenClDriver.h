#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace hitsimple::gpu::detail {

using ClInt = std::int32_t;
using ClUInt = std::uint32_t;
using ClBool = ClUInt;
using ClBitfield = std::uint64_t;
using ClDeviceType = ClBitfield;
using ClMemFlags = ClBitfield;
using ClContextProperties = std::intptr_t;
using ClPlatformInfo = ClUInt;
using ClDeviceInfo = ClUInt;
using ClProgramBuildInfo = ClUInt;
using ClPlatformId = void*;
using ClDeviceId = void*;
using ClContext = void*;
using ClCommandQueue = void*;
using ClMem = void*;
using ClProgram = void*;
using ClKernel = void*;
using ClEvent = void*;
using ContextNotifyFn = void (*)(const char*, const void*, std::size_t, void*);
using ProgramNotifyFn = void (*)(ClProgram, void*);

class OpenClDriver final {
public:
  using GetPlatformIdsFn = ClInt (*)(ClUInt, ClPlatformId*, ClUInt*);
  using GetDeviceIdsFn = ClInt (*)(ClPlatformId, ClDeviceType, ClUInt,
                                   ClDeviceId*, ClUInt*);
  using GetDeviceInfoFn = ClInt (*)(ClDeviceId, ClDeviceInfo, std::size_t,
                                    void*, std::size_t*);
  using CreateContextFn = ClContext (*)(const ClContextProperties*, ClUInt,
                                        const ClDeviceId*, ContextNotifyFn,
                                        void*, ClInt*);
  using ReleaseContextFn = ClInt (*)(ClContext);
  using CreateCommandQueueFn = ClCommandQueue (*)(ClContext, ClDeviceId,
                                                   ClBitfield, ClInt*);
  using ReleaseCommandQueueFn = ClInt (*)(ClCommandQueue);
  using CreateBufferFn = ClMem (*)(ClContext, ClMemFlags, std::size_t,
                                   void*, ClInt*);
  using ReleaseMemObjectFn = ClInt (*)(ClMem);
  using CreateProgramWithSourceFn = ClProgram (*)(ClContext, ClUInt,
                                                  const char**, const std::size_t*,
                                                  ClInt*);
  using BuildProgramFn = ClInt (*)(ClProgram, ClUInt, const ClDeviceId*,
                                   const char*, ProgramNotifyFn, void*);
  using GetProgramBuildInfoFn = ClInt (*)(ClProgram, ClDeviceId,
                                          ClProgramBuildInfo, std::size_t,
                                          void*, std::size_t*);
  using ReleaseProgramFn = ClInt (*)(ClProgram);
  using CreateKernelFn = ClKernel (*)(ClProgram, const char*, ClInt*);
  using ReleaseKernelFn = ClInt (*)(ClKernel);
  using SetKernelArgFn = ClInt (*)(ClKernel, ClUInt, std::size_t, const void*);
  using EnqueueWriteBufferFn = ClInt (*)(ClCommandQueue, ClMem, ClBool,
                                         std::size_t, std::size_t, const void*,
                                         ClUInt, const ClEvent*, ClEvent*);
  using EnqueueReadBufferFn = ClInt (*)(ClCommandQueue, ClMem, ClBool,
                                        std::size_t, std::size_t, void*, ClUInt,
                                        const ClEvent*, ClEvent*);
  using EnqueueCopyBufferFn = ClInt (*)(ClCommandQueue, ClMem, ClMem,
                                        std::size_t, std::size_t, std::size_t,
                                        ClUInt, const ClEvent*, ClEvent*);
  using EnqueueNdRangeKernelFn = ClInt (*)(ClCommandQueue, ClKernel, ClUInt,
                                           const std::size_t*, const std::size_t*,
                                           const std::size_t*, ClUInt,
                                           const ClEvent*, ClEvent*);
  using FinishFn = ClInt (*)(ClCommandQueue);

  OpenClDriver() = default;
  ~OpenClDriver();
  OpenClDriver(const OpenClDriver&) = delete;
  OpenClDriver& operator=(const OpenClDriver&) = delete;

  bool load(std::string& error);

  GetPlatformIdsFn getPlatformIds_ = nullptr;
  GetDeviceIdsFn getDeviceIds_ = nullptr;
  GetDeviceInfoFn getDeviceInfo_ = nullptr;
  CreateContextFn createContext_ = nullptr;
  ReleaseContextFn releaseContext_ = nullptr;
  CreateCommandQueueFn createCommandQueue_ = nullptr;
  ReleaseCommandQueueFn releaseCommandQueue_ = nullptr;
  CreateBufferFn createBuffer_ = nullptr;
  ReleaseMemObjectFn releaseMemObject_ = nullptr;
  CreateProgramWithSourceFn createProgramWithSource_ = nullptr;
  BuildProgramFn buildProgram_ = nullptr;
  GetProgramBuildInfoFn getProgramBuildInfo_ = nullptr;
  ReleaseProgramFn releaseProgram_ = nullptr;
  CreateKernelFn createKernel_ = nullptr;
  ReleaseKernelFn releaseKernel_ = nullptr;
  SetKernelArgFn setKernelArg_ = nullptr;
  EnqueueWriteBufferFn enqueueWriteBuffer_ = nullptr;
  EnqueueReadBufferFn enqueueReadBuffer_ = nullptr;
  EnqueueCopyBufferFn enqueueCopyBuffer_ = nullptr;
  EnqueueNdRangeKernelFn enqueueNdRangeKernel_ = nullptr;
  FinishFn finish_ = nullptr;

private:
  template <typename Function>
  bool loadSymbol(Function& target, const char* name, std::string& error);

  void* handle_ = nullptr;
};

} // namespace hitsimple::gpu::detail
