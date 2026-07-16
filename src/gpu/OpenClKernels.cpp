#include "OpenClKernels.h"

namespace hitsimple::gpu::detail {

const char* openClSource() {
  return R"opencl(
__kernel void hs_propagate_reachability(
    __global const uint* edgeFrom,
    __global const uint* edgeTo,
    const uint edgeCount,
    __global uint* reachable,
    __global uint* changed) {
  const uint index = get_global_id(0);
  if (index >= edgeCount || reachable[edgeFrom[index]] == 0u) {
    return;
  }
  const int previous = atom_or((volatile __global int*)&reachable[edgeTo[index]], 1);
  if (previous == 0) {
    atom_xchg((volatile __global int*)changed, 1);
  }
}

__kernel void hs_propagate_liveness(
    __global const uint* successorOffsets,
    __global const uint* successors,
    __global const uint* uses,
    __global const uint* definitions,
    __global uint* liveIn,
    const uint blockCount,
    const uint wordCount,
    __global uint* changed) {
  const uint index = get_global_id(0);
  const uint itemCount = blockCount * wordCount;
  if (index >= itemCount) {
    return;
  }
  const uint block = index / wordCount;
  const uint word = index % wordCount;
  uint successorLive = 0u;
  for (uint edge = successorOffsets[block]; edge < successorOffsets[block + 1u]; ++edge) {
    successorLive |= liveIn[successors[edge] * wordCount + word];
  }
  const uint next = uses[index] | (successorLive & ~definitions[index]);
  const int previous = atom_or((volatile __global int*)&liveIn[index], (int)next);
  if ((uint)previous != ((uint)previous | next)) {
    atom_xchg((volatile __global int*)changed, 1);
  }
}

__kernel void hs_propagate_view_ranges(
    __global const uint* inputStates,
    __global const uint* inputObjects,
    __global const uint* inputOffsets,
    __global const uint* inputLengths,
    __global uint* outputStates,
    __global uint* outputObjects,
    __global uint* outputOffsets,
    __global uint* outputLengths,
    __global const uint* sources,
    __global const uint* destinations,
    const uint pairCount,
    __global uint* changed) {
  const uint index = get_global_id(0);
  if (index >= pairCount) {
    return;
  }
  const uint source = sources[index];
  const uint destination = destinations[index];
  if (inputStates[source] == 0u || outputStates[destination] != 0u) {
    return;
  }
  outputObjects[destination] = inputObjects[source];
  outputOffsets[destination] = inputOffsets[source];
  outputLengths[destination] = inputLengths[source];
  mem_fence(CLK_GLOBAL_MEM_FENCE);
  if (atom_cmpxchg((volatile __global int*)&outputStates[destination], 0, 1) == 0) {
    atom_xchg((volatile __global int*)changed, 1);
  }
}
)opencl";
}

} // namespace hitsimple::gpu::detail
