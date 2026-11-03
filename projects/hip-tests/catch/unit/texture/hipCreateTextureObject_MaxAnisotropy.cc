/*
 * Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
 *
 * SPDX-License-Identifier: MIT
 */

#include <hip_test_common.hh>

/**
 * @addtogroup hipCreateTextureObject hipCreateTextureObject
 * @{
 * @ingroup TextureTest
 */

namespace {

constexpr int kWidth = 64;
constexpr int kHeight = 64;

// Anisotropy ratios the hardware can represent. The sampler SRD stores log2 of
// the ratio in a 3-bit field, so only powers of two up to 16 are distinct.
constexpr unsigned int kSupportedRatios[] = {1, 2, 4, 8, 16};

__global__ void tex2DKernel(float* outputData, hipTextureObject_t textureObject, int width) {
#if !__HIP_NO_IMAGE_SUPPORT
  int x = blockIdx.x * blockDim.x + threadIdx.x;
  int y = blockIdx.y * blockDim.y + threadIdx.y;
  outputData[y * width + x] = tex2D<float>(textureObject, x, y);
#endif
}

// Creates a texture object over a 2D array with the requested anisotropy and
// filter mode. The caller owns the returned object and the array.
hipTextureObject_t CreateTexture(hipArray_t* array, unsigned int maxAnisotropy,
                                 hipTextureFilterMode filterMode, float* hostData) {
  hipChannelFormatDesc channelDesc = hipCreateChannelDesc<float>();
  HIP_CHECK(hipMallocArray(array, &channelDesc, kWidth, kHeight));
  HIP_CHECK(hipMemcpy2DToArray(*array, 0, 0, hostData, kWidth * sizeof(float),
                               kWidth * sizeof(float), kHeight, hipMemcpyHostToDevice));

  hipResourceDesc resDesc;
  memset(&resDesc, 0, sizeof(resDesc));
  resDesc.resType = hipResourceTypeArray;
  resDesc.res.array.array = *array;

  hipTextureDesc texDesc;
  memset(&texDesc, 0, sizeof(texDesc));
  texDesc.addressMode[0] = hipAddressModeClamp;
  texDesc.addressMode[1] = hipAddressModeClamp;
  texDesc.filterMode = filterMode;
  texDesc.readMode = hipReadModeElementType;
  texDesc.normalizedCoords = 0;
  texDesc.maxAnisotropy = maxAnisotropy;

  hipTextureObject_t textureObject = 0;
  HIP_CHECK(hipCreateTextureObject(&textureObject, &resDesc, &texDesc, nullptr));
  return textureObject;
}

}  // anonymous namespace

/**
 * Test Description
 * ------------------------
 *  - Validates that [hipCreateTextureObject](@ref hipCreateTextureObject) accepts a nonzero
 *    `hipTextureDesc::maxAnisotropy` and enables anisotropic filtering:
 *    -# When `maxAnisotropy` is each of the supported ratios 1, 2, 4, 8 and 16
 *      - Expected output: return `hipSuccess`
 *    -# When `maxAnisotropy` is 0, meaning isotropic filtering
 *      - Expected output: return `hipSuccess`
 *    -# When `maxAnisotropy` is not a power of two, so clamps down to the closest supported ratio
 *      - Expected output: return `hipSuccess`
 *    -# When `maxAnisotropy` exceeds the maximum supported ratio of 16
 *      - Expected output: return `hipSuccess`, the ratio saturating at 16:1 rather than being
 *        rejected
 *    -# When `maxAnisotropy` is combined with either point or linear filtering, both of which
 *       support anisotropy
 *      - Expected output: return `hipSuccess`
 *  - Validates that the requested value round-trips through
 *    [hipGetTextureObjectTextureDesc](@ref hipGetTextureObjectTextureDesc).
 *  - Validates that sampling a texture with anisotropic filtering enabled still returns the
 *    expected texel values, so that enabling anisotropy does not corrupt ordinary reads.
 *
 *  Prior to anisotropy support the runtime rejected any nonzero `maxAnisotropy` with
 *  `hipErrorNotSupported`, so every nonzero case below is a regression guard.
 * Test source
 * ------------------------
 *  - unit/texture/hipCreateTextureObject_MaxAnisotropy.cc
 * Test requirements
 * ------------------------
 *  - Textures supported on device
 */
HIP_TEST_CASE(Unit_hipCreateTextureObject_MaxAnisotropy_Positive) {
  CHECK_IMAGE_SUPPORT

  std::vector<float> hostData(kWidth * kHeight);
  for (int i = 0; i < kWidth * kHeight; i++) {
    hostData[i] = static_cast<float>(i);
  }

  // Every supported ratio, plus 0 (isotropic), non-power-of-two values that
  // clamp down to the closest supported ratio, and values above the maximum of
  // 16 which leave filtering isotropic. None of these may be rejected.
  const unsigned int anisotropyValues[] = {0, 1, 2, 3, 4, 7, 8, 15, 16, 17, 32};
  // Anisotropy applies to point sampling as well as linear, so exercise both.
  const hipTextureFilterMode filterModes[] = {hipFilterModePoint, hipFilterModeLinear};

  for (hipTextureFilterMode filterMode : filterModes) {
    for (unsigned int maxAnisotropy : anisotropyValues) {
      INFO("filterMode = " << static_cast<int>(filterMode)
                           << ", maxAnisotropy = " << maxAnisotropy);

      hipArray_t array = nullptr;
      hipTextureObject_t textureObject =
          CreateTexture(&array, maxAnisotropy, filterMode, hostData.data());
      REQUIRE(textureObject != 0);

      HIP_CHECK(hipDestroyTextureObject(textureObject));
      HIP_CHECK(hipFreeArray(array));
    }
  }
}

/**
 * Test Description
 * ------------------------
 *  - Validates that `hipTextureDesc::maxAnisotropy` survives a round trip through
 *    [hipGetTextureObjectTextureDesc](@ref hipGetTextureObjectTextureDesc) for every supported
 *    anisotropy ratio.
 * Test source
 * ------------------------
 *  - unit/texture/hipCreateTextureObject_MaxAnisotropy.cc
 * Test requirements
 * ------------------------
 *  - Textures supported on device
 */
HIP_TEST_CASE(Unit_hipCreateTextureObject_MaxAnisotropy_RoundTrip) {
  CHECK_IMAGE_SUPPORT

  std::vector<float> hostData(kWidth * kHeight, 1.0f);

  for (unsigned int maxAnisotropy : kSupportedRatios) {
    INFO("maxAnisotropy = " << maxAnisotropy);

    hipArray_t array = nullptr;
    hipTextureObject_t textureObject =
        CreateTexture(&array, maxAnisotropy, hipFilterModeLinear, hostData.data());

    hipTextureDesc queriedDesc;
    memset(&queriedDesc, 0, sizeof(queriedDesc));
    HIP_CHECK(hipGetTextureObjectTextureDesc(&queriedDesc, textureObject));

    REQUIRE(queriedDesc.maxAnisotropy == maxAnisotropy);
    REQUIRE(queriedDesc.filterMode == hipFilterModeLinear);

    HIP_CHECK(hipDestroyTextureObject(textureObject));
    HIP_CHECK(hipFreeArray(array));
  }
}

/**
 * Test Description
 * ------------------------
 *  - Validates that enabling anisotropic filtering does not disturb ordinary texture reads.
 *    Samples a 2D texture at exact texel centres with anisotropy disabled and with the maximum
 *    ratio, and requires both to return the stored values.
 *
 *    Sampling at texel centres is isotropic regardless of the anisotropy setting, so the two
 *    results must agree - any difference would indicate the sampler was misprogrammed.
 * Test source
 * ------------------------
 *  - unit/texture/hipCreateTextureObject_MaxAnisotropy.cc
 * Test requirements
 * ------------------------
 *  - Textures supported on device
 */
HIP_TEST_CASE(Unit_hipCreateTextureObject_MaxAnisotropy_Sampling) {
  CHECK_IMAGE_SUPPORT

  std::vector<float> hostData(kWidth * kHeight);
  for (int i = 0; i < kWidth * kHeight; i++) {
    hostData[i] = static_cast<float>(i);
  }

  float* deviceOutput = nullptr;
  HIP_CHECK(hipMalloc(&deviceOutput, kWidth * kHeight * sizeof(float)));

  const unsigned int anisotropyValues[] = {0, 16};
  std::vector<std::vector<float>> results;

  for (unsigned int maxAnisotropy : anisotropyValues) {
    INFO("maxAnisotropy = " << maxAnisotropy);

    hipArray_t array = nullptr;
    hipTextureObject_t textureObject =
        CreateTexture(&array, maxAnisotropy, hipFilterModeLinear, hostData.data());

    HIP_CHECK(hipMemset(deviceOutput, 0, kWidth * kHeight * sizeof(float)));

    dim3 blockDim(16, 16, 1);
    dim3 gridDim(kWidth / blockDim.x, kHeight / blockDim.y, 1);
    hipLaunchKernelGGL(tex2DKernel, gridDim, blockDim, 0, 0, deviceOutput, textureObject, kWidth);
    HIP_CHECK(hipGetLastError());
    HIP_CHECK(hipDeviceSynchronize());

    std::vector<float> output(kWidth * kHeight);
    HIP_CHECK(hipMemcpy(output.data(), deviceOutput, kWidth * kHeight * sizeof(float),
                        hipMemcpyDeviceToHost));

    for (int i = 0; i < kWidth * kHeight; i++) {
      INFO("index = " << i);
      REQUIRE(output[i] == hostData[i]);
    }

    results.push_back(std::move(output));

    HIP_CHECK(hipDestroyTextureObject(textureObject));
    HIP_CHECK(hipFreeArray(array));
  }

  // Texel-centre sampling must be unaffected by the anisotropy setting.
  REQUIRE(results.size() == 2);
  for (int i = 0; i < kWidth * kHeight; i++) {
    INFO("index = " << i);
    REQUIRE(results[0][i] == results[1][i]);
  }

  HIP_CHECK(hipFree(deviceOutput));
}

/**
* End doxygen group TextureTest.
* @}
*/
