#include "Graphics/RHI/RHIRayTracing.h"
#include "Graphics/RHI/RHIResource.h"
#include <gtest/gtest.h>

namespace zen
{
namespace
{
RHIRayTracingCapabilities Capabilities()
{
    RHIRayTracingCapabilities result;
    result.pipeline                      = true;
    result.shaderGroupHandleSize         = 24;
    result.shaderGroupHandleAlignment    = 32;
    result.shaderGroupBaseAlignment      = 64;
    result.maxShaderGroupStride          = 4096;
    result.maxRayRecursionDepth          = 31;
    result.maxRayDispatchInvocationCount = 1u << 30;
    result.maxRayHitAttributeSize        = 32;
    return result;
}
} // namespace

TEST(RayTracingLayout, AlignsEveryRegionAndKeepsRaygenSizeEqualToStride)
{
    RHIShaderBindingTableLayout layout;
    ASSERT_TRUE(BuildShaderBindingTableLayout(Capabilities(), {1, 3, 2, 1}, layout));
    EXPECT_EQ(layout.alignment, 64u);
    EXPECT_EQ(layout.size, 288u);
    const uint64_t offsets[] = {0, 64, 192, 256};
    const uint64_t sizes[]   = {32, 96, 64, 32};
    for (uint32_t region = 0; region < RHIShaderBindingTableLayout::Count; ++region)
    {
        EXPECT_EQ(layout.regions[region].offset, offsets[region]);
        EXPECT_EQ(layout.regions[region].stride, 32u);
        EXPECT_EQ(layout.regions[region].size, sizes[region]);
    }
}

TEST(RayTracingLayout, EmptyRegionsUseZeroAddressesAndDoNotReserveRecords)
{
    RHIShaderBindingTableLayout layout;
    ASSERT_TRUE(BuildShaderBindingTableLayout(Capabilities(), {1, 0, 2, 0}, layout));
    EXPECT_EQ(layout.size, 128u);
    EXPECT_EQ(layout.regions[RHIShaderBindingTableLayout::Hit].offset, 64u);
    for (uint32_t region : {RHIShaderBindingTableLayout::Miss, RHIShaderBindingTableLayout::Callable})
    {
        EXPECT_EQ(layout.regions[region].offset, 0u);
        EXPECT_EQ(layout.regions[region].stride, 0u);
        EXPECT_EQ(layout.regions[region].size, 0u);
    }
}

TEST(RayTracingLayout, RejectsInvalidCapabilitiesAndCountsWithoutPublishingPartialLayout)
{
    for (uint32_t scenario = 0; scenario < 8; ++scenario)
    {
        SCOPED_TRACE(scenario);
        RHIRayTracingCapabilities                                capabilities = Capabilities();
        std::array<uint32_t, RHIShaderBindingTableLayout::Count> counts{1, 1, 1, 0};
        switch (scenario)
        {
            case 0: capabilities.pipeline = false; break;
            case 1: capabilities.shaderGroupHandleSize = 0; break;
            case 2: capabilities.shaderGroupHandleAlignment = 3; break;
            case 3: capabilities.shaderGroupBaseAlignment = 0; break;
            case 4: capabilities.maxShaderGroupStride = 31; break;
            case 5: counts[0] = 0; break;
            case 6: counts[0] = 2; break;
            case 7: capabilities.maxRayRecursionDepth = 0; break;
        }
        RHIShaderBindingTableLayout layout;
        layout.size = 123;
        EXPECT_FALSE(BuildShaderBindingTableLayout(capabilities, counts, layout));
        EXPECT_EQ(layout.size, 0u);
        EXPECT_EQ(layout.alignment, 0u);
        for (const RHIShaderBindingTableRegion& region : layout.regions)
        {
            EXPECT_EQ(region.offset, 0u);
            EXPECT_EQ(region.stride, 0u);
            EXPECT_EQ(region.size, 0u);
        }
    }
}

TEST(RayTracingLayout, RejectsCombinedRegionOverflow)
{
    RHIRayTracingCapabilities capabilities = Capabilities();
    capabilities.shaderGroupHandleSize     = 1u << 31;
    capabilities.maxShaderGroupStride      = 1u << 31;
    RHIShaderBindingTableLayout layout;
    EXPECT_FALSE(BuildShaderBindingTableLayout(capabilities, {1, UINT32_MAX, UINT32_MAX, UINT32_MAX}, layout));
    EXPECT_EQ(layout.size, 0u);
}

TEST(RayTracingLayout, ShaderModulesPreserveDistinctMissAndHitIndices)
{
    RHIShaderGroupSPIRV modules;
    EXPECT_EQ(modules.AppendStageSPIRV(RHIShaderStage::eRayGeneration, {1}), 0u);
    EXPECT_EQ(modules.AppendStageSPIRV(RHIShaderStage::eMiss, {2}), 1u);
    EXPECT_EQ(modules.AppendStageSPIRV(RHIShaderStage::eMiss, {3}), 2u);
    EXPECT_EQ(modules.AppendStageSPIRV(RHIShaderStage::eClosestHit, {4}), 3u);
    EXPECT_EQ(modules.AppendStageSPIRV(RHIShaderStage::eAnyHit, {5}), 4u);
    ASSERT_EQ(modules.GetStageCount(), 5u);
    ASSERT_EQ(modules.GetStageSPIRV(RHIShaderStage::eMiss).size(), 1u);
    EXPECT_EQ(modules.GetStageSPIRV(RHIShaderStage::eMiss)[0], 2u);
    EXPECT_EQ(modules.GetModules()[2].stage, RHIShaderStage::eMiss);
    ASSERT_EQ(modules.GetModules()[2].code.size(), 1u);
    EXPECT_EQ(modules.GetModules()[2].code[0], 3u);
    modules.SetStageSPIRV(RHIShaderStage::eMiss, {6});
    EXPECT_EQ(modules.GetStageCount(), 5u);
    ASSERT_EQ(modules.GetModules()[1].code.size(), 1u);
    EXPECT_EQ(modules.GetModules()[1].code[0], 6u);
    EXPECT_EQ(modules.GetModules()[2].code[0], 3u);
}

TEST(RayTracingLayout, GroupsValidateStageIndicesAndKeepDistinctMissRecords)
{
    RHIShaderGroupSPIRV modules;
    modules.AppendStageSPIRV(RHIShaderStage::eRayGeneration, {1});
    modules.AppendStageSPIRV(RHIShaderStage::eMiss, {2});
    modules.AppendStageSPIRV(RHIShaderStage::eMiss, {3});
    modules.AppendStageSPIRV(RHIShaderStage::eClosestHit, {4});
    modules.AppendStageSPIRV(RHIShaderStage::eAnyHit, {5});
    modules.AppendStageSPIRV(RHIShaderStage::eCallable, {6});
    const HeapVector<RHIRayTracingShaderGroup> groups = {{RHIRayTracingShaderGroupType::eMiss, 2},
                                                         {RHIRayTracingShaderGroupType::eHit, kUnusedRayTracingShader, 3, 4},
                                                         {RHIRayTracingShaderGroupType::eRayGeneration, 0},
                                                         {RHIRayTracingShaderGroupType::eMiss, 1},
                                                         {RHIRayTracingShaderGroupType::eCallable, 5}};
    for (uint32_t scenario = 0; scenario < 10; ++scenario)
    {
        SCOPED_TRACE(scenario);
        HeapVector<RHIRayTracingShaderGroup> candidate = groups;
        switch (scenario)
        {
            case 0: break;
            case 1: candidate[0].generalShader = 0; break;
            case 2: candidate[1].closestHitShader = 4; break;
            case 3: candidate[1].anyHitShader = 99; break;
            case 4: candidate[1].generalShader = 1; break;
            case 5: candidate[1].closestHitShader = candidate[1].anyHitShader = kUnusedRayTracingShader; break;
            case 6: candidate[2] = candidate[0]; break;
            case 7: candidate.push_back(candidate[2]); break;
            case 8: candidate[4].type = static_cast<RHIRayTracingShaderGroupType>(99); break;
            case 9: candidate[0].anyHitShader = 4; break;
        }
        std::array<uint32_t, RHIShaderBindingTableLayout::Count> counts{99, 99, 99, 99};
        EXPECT_EQ(ValidateRayTracingShaderGroups(modules, candidate, counts), scenario == 0);
        const std::array<uint32_t, RHIShaderBindingTableLayout::Count> expected =
            scenario == 0 ? std::array<uint32_t, RHIShaderBindingTableLayout::Count>{1, 2, 1, 1}
                          : std::array<uint32_t, RHIShaderBindingTableLayout::Count>{};
        EXPECT_EQ(counts, expected);
    }
    std::array<uint32_t, RHIShaderBindingTableLayout::Count> counts{};
    EXPECT_TRUE(ValidateRayTracingShaderGroups(
        modules,
        {{RHIRayTracingShaderGroupType::eRayGeneration, 0},
         {RHIRayTracingShaderGroupType::eHit, kUnusedRayTracingShader, kUnusedRayTracingShader, 4}},
        counts));
    modules.AppendStageSPIRV(RHIShaderStage::eCompute, {7});
    EXPECT_FALSE(ValidateRayTracingShaderGroups(modules, groups, counts));
    EXPECT_EQ(counts, (std::array<uint32_t, RHIShaderBindingTableLayout::Count>{}));
}

TEST(RayTracingLayout, LaunchLimitsCheckAxesAndTotalWithoutIntegerOverflow)
{
    RHIGPUInfo info;
    info.rayTracing                               = Capabilities();
    info.maxComputeWorkGroupCount                 = {16, 8, 4};
    info.maxComputeWorkGroupSize                  = {8, 4, 2};
    info.rayTracing.maxRayDispatchInvocationCount = 1024;
    EXPECT_TRUE(info.IsTraceRaysWithinLimits(128, 8, 1));
    EXPECT_TRUE(info.IsTraceRaysWithinLimits(16, 8, 8));
    EXPECT_TRUE(info.IsTraceRaysWithinLimits(128, 32, 0));
    EXPECT_FALSE(info.IsTraceRaysWithinLimits(129, 1, 1));
    EXPECT_FALSE(info.IsTraceRaysWithinLimits(1, 33, 1));
    EXPECT_FALSE(info.IsTraceRaysWithinLimits(1, 1, 9));
    EXPECT_FALSE(info.IsTraceRaysWithinLimits(128, 9, 1));
    info.maxComputeWorkGroupCount.fill(UINT32_MAX);
    info.maxComputeWorkGroupSize.fill(UINT32_MAX);
    EXPECT_FALSE(info.IsTraceRaysWithinLimits(UINT32_MAX, UINT32_MAX, UINT32_MAX));
    info.rayTracing.pipeline = false;
    EXPECT_FALSE(info.IsTraceRaysWithinLimits(1, 1, 1));
}
} // namespace zen
