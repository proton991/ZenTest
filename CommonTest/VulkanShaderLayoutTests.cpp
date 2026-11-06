#include "Graphics/VulkanRHI/VulkanPipeline.h"
#include "Graphics/VulkanRHI/VulkanDescriptorPool.h"
#include "Graphics/RHI/RHIOptions.h"
#include <gtest/gtest.h>
#include <limits>

namespace
{
using namespace zen;

class VulkanShaderLayoutLimitTest : public testing::Test
{
protected:
    RHIShaderGroupInfo                                 info;
    VkPhysicalDeviceLimits                             limits{};
    VkPhysicalDeviceDescriptorIndexingProperties       indexing{};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR acceleration{};
    RHIBindlessHeapCapacities                          capacities;

    void SetUp() override
    {
        info.SRDTable.resize(3);
        capacities.slots.fill(0);
        limits.maxPushConstantsSize                                = 128;
        limits.maxUniformBufferRange                               = 65536;
        limits.maxDescriptorSetSamplers                            = 1024;
        limits.maxDescriptorSetUniformBuffers                      = 1024;
        limits.maxDescriptorSetUniformBuffersDynamic               = 1024;
        limits.maxDescriptorSetStorageBuffers                      = 1024;
        limits.maxDescriptorSetSampledImages                       = 1024;
        limits.maxDescriptorSetStorageImages                       = 1024;
        limits.maxDescriptorSetInputAttachments                    = 1024;
        limits.maxPerStageDescriptorSamplers                       = 1024;
        limits.maxPerStageDescriptorUniformBuffers                 = 1024;
        limits.maxPerStageDescriptorStorageBuffers                 = 1024;
        limits.maxPerStageDescriptorSampledImages                  = 1024;
        limits.maxPerStageDescriptorStorageImages                  = 1024;
        limits.maxPerStageDescriptorInputAttachments               = 1024;
        limits.maxPerStageResources                                = 4096;
        indexing.maxPerStageUpdateAfterBindResources               = 4096;
        indexing.maxDescriptorSetUpdateAfterBindSamplers           = 4096;
        indexing.maxDescriptorSetUpdateAfterBindSampledImages      = 4096;
        indexing.maxPerStageDescriptorUpdateAfterBindSamplers      = 4096;
        indexing.maxPerStageDescriptorUpdateAfterBindSampledImages = 4096;
        acceleration.maxDescriptorSetAccelerationStructures        = 1024;
        acceleration.maxPerStageDescriptorAccelerationStructures   = 1024;
    }

    RHIShaderResourceDescriptor& Add(RHIShaderResourceType  type,
                                     uint32_t               count,
                                     RHIShaderStageFlagBits stage = RHIShaderStageFlagBits::eFragment,
                                     uint32_t               set   = 1)
    {
        RHIShaderResourceDescriptor descriptor;
        descriptor.type      = type;
        descriptor.arraySize = count;
        descriptor.set       = set;
        descriptor.binding   = static_cast<uint32_t>(info.SRDTable[set].size());
        descriptor.stageFlags.SetFlag(stage);
        info.SRDTable[set].push_back(descriptor);
        return info.SRDTable[set].back();
    }

    bool Fits() const
    {
        return ShaderLayoutFitsLimits(info, limits, indexing, acceleration, capacities);
    }
};

struct DescriptorLimitCase
{
    RHIShaderResourceType type;
    uint32_t VkPhysicalDeviceLimits::* layoutLimit;
    uint32_t VkPhysicalDeviceLimits::* stageLimit;
};
} // namespace

TEST_F(VulkanShaderLayoutLimitTest, EveryOrdinaryDescriptorClassChecksLayoutAndStageArrayCounts)
{
    const DescriptorLimitCase cases[] = {
        {RHIShaderResourceType::eSampler, &VkPhysicalDeviceLimits::maxDescriptorSetSamplers,
         &VkPhysicalDeviceLimits::maxPerStageDescriptorSamplers},
        {RHIShaderResourceType::eTexture, &VkPhysicalDeviceLimits::maxDescriptorSetSampledImages,
         &VkPhysicalDeviceLimits::maxPerStageDescriptorSampledImages},
        {RHIShaderResourceType::eSamplerWithTexture, &VkPhysicalDeviceLimits::maxDescriptorSetSamplers,
         &VkPhysicalDeviceLimits::maxPerStageDescriptorSamplers},
        {RHIShaderResourceType::eSamplerWithTexture, &VkPhysicalDeviceLimits::maxDescriptorSetSampledImages,
         &VkPhysicalDeviceLimits::maxPerStageDescriptorSampledImages},
        {RHIShaderResourceType::eImage, &VkPhysicalDeviceLimits::maxDescriptorSetStorageImages,
         &VkPhysicalDeviceLimits::maxPerStageDescriptorStorageImages},
        {RHIShaderResourceType::eTextureBuffer, &VkPhysicalDeviceLimits::maxDescriptorSetSampledImages,
         &VkPhysicalDeviceLimits::maxPerStageDescriptorSampledImages},
        {RHIShaderResourceType::eSamplerWithTextureBuffer, &VkPhysicalDeviceLimits::maxDescriptorSetSampledImages,
         &VkPhysicalDeviceLimits::maxPerStageDescriptorSampledImages},
        {RHIShaderResourceType::eImageBuffer, &VkPhysicalDeviceLimits::maxDescriptorSetStorageImages,
         &VkPhysicalDeviceLimits::maxPerStageDescriptorStorageImages},
        {RHIShaderResourceType::eUniformBuffer, &VkPhysicalDeviceLimits::maxDescriptorSetUniformBuffers,
         &VkPhysicalDeviceLimits::maxPerStageDescriptorUniformBuffers},
        {RHIShaderResourceType::eUniformBuffer, &VkPhysicalDeviceLimits::maxDescriptorSetUniformBuffersDynamic,
         &VkPhysicalDeviceLimits::maxPerStageDescriptorUniformBuffers},
        {RHIShaderResourceType::eStorageBuffer, &VkPhysicalDeviceLimits::maxDescriptorSetStorageBuffers,
         &VkPhysicalDeviceLimits::maxPerStageDescriptorStorageBuffers},
        {RHIShaderResourceType::eInputAttachment, &VkPhysicalDeviceLimits::maxDescriptorSetInputAttachments,
         &VkPhysicalDeviceLimits::maxPerStageDescriptorInputAttachments}};
    for (const DescriptorLimitCase& testCase : cases)
    {
        SCOPED_TRACE(ToUnderlying(testCase.type));
        info.SRDTable[1].clear();
        Add(testCase.type, 8);
        limits.*testCase.layoutLimit = 8;
        limits.*testCase.stageLimit  = 8;
        EXPECT_TRUE(Fits());
        limits.*testCase.layoutLimit = 7;
        EXPECT_FALSE(Fits());
        limits.*testCase.layoutLimit = 8;
        limits.*testCase.stageLimit  = 7;
        EXPECT_FALSE(Fits());
        limits.*testCase.layoutLimit = 1024;
        limits.*testCase.stageLimit  = 1024;
    }
}

TEST_F(VulkanShaderLayoutLimitTest, DynamicUniformElementsAggregateAcrossSetsAndVisibleStages)
{
    Add(RHIShaderResourceType::eUniformBuffer, 3).stageFlags.SetFlag(RHIShaderStageFlagBits::eVertex);
    Add(RHIShaderResourceType::eUniformBuffer, 1, RHIShaderStageFlagBits::eFragment, 2);
    limits.maxDescriptorSetUniformBuffersDynamic = 4;
    limits.maxDescriptorSetUniformBuffers        = 4;
    limits.maxPerStageDescriptorUniformBuffers   = 4;
    EXPECT_TRUE(Fits());
    limits.maxDescriptorSetUniformBuffersDynamic = 3;
    EXPECT_FALSE(Fits());
    limits.maxDescriptorSetUniformBuffersDynamic = 4;
    limits.maxDescriptorSetUniformBuffers        = 3;
    EXPECT_FALSE(Fits());
    limits.maxDescriptorSetUniformBuffers      = 4;
    limits.maxPerStageDescriptorUniformBuffers = 3;
    EXPECT_FALSE(Fits());
    info.SRDTable[2][0].stageFlags.Clear();
    info.SRDTable[2][0].stageFlags.SetFlag(RHIShaderStageFlagBits::eCompute);
    EXPECT_TRUE(Fits());
}

TEST_F(VulkanShaderLayoutLimitTest, GlobalAndOrdinarySamplerAndImageBudgetsAreCombined)
{
    Add(RHIShaderResourceType::eSamplerWithTexture, 4);
    Add(RHIShaderResourceType::eSamplerWithTexture, 4, RHIShaderStageFlagBits::eVertex, 2);
    capacities.slots                = {2, 2, 8, 2, 2};
    limits.maxDescriptorSetSamplers = limits.maxDescriptorSetSampledImages = 8;
    limits.maxPerStageDescriptorSamplers = limits.maxPerStageDescriptorSampledImages = 8;
    indexing.maxDescriptorSetUpdateAfterBindSamplers = indexing.maxDescriptorSetUpdateAfterBindSampledImages = 16;
    indexing.maxPerStageDescriptorUpdateAfterBindSamplers = indexing.maxPerStageDescriptorUpdateAfterBindSampledImages = 12;
    EXPECT_TRUE(Fits()); // Set zero is absent from reflection, but present in the native layout.
    --indexing.maxDescriptorSetUpdateAfterBindSamplers;
    EXPECT_FALSE(Fits());
    ++indexing.maxDescriptorSetUpdateAfterBindSamplers;
    --indexing.maxDescriptorSetUpdateAfterBindSampledImages;
    EXPECT_FALSE(Fits());
    ++indexing.maxDescriptorSetUpdateAfterBindSampledImages;
    --indexing.maxPerStageDescriptorUpdateAfterBindSamplers;
    EXPECT_FALSE(Fits());
    ++indexing.maxPerStageDescriptorUpdateAfterBindSamplers;
    --indexing.maxPerStageDescriptorUpdateAfterBindSampledImages;
    EXPECT_FALSE(Fits());
}

TEST_F(VulkanShaderLayoutLimitTest, CombinedBudgetsUseTheUpdateAfterBindLimitsAlone)
{
    Add(RHIShaderResourceType::eSamplerWithTexture, 4);
    capacities.slots = {1, 1, 4, 1, 1};
    EXPECT_TRUE(Fits());
    // Each value stays below its ordinary limit, which must not widen the native limit.
    indexing.maxDescriptorSetUpdateAfterBindSamplers = 7;
    EXPECT_FALSE(Fits());
    indexing.maxDescriptorSetUpdateAfterBindSamplers      = 8;
    indexing.maxPerStageDescriptorUpdateAfterBindSamplers = 7;
    EXPECT_FALSE(Fits());
    indexing.maxPerStageDescriptorUpdateAfterBindSamplers = 8;
    indexing.maxDescriptorSetUpdateAfterBindSampledImages = 7;
    EXPECT_FALSE(Fits());
    indexing.maxDescriptorSetUpdateAfterBindSampledImages      = 8;
    indexing.maxPerStageDescriptorUpdateAfterBindSampledImages = 7;
    EXPECT_FALSE(Fits());
}

TEST_F(VulkanShaderLayoutLimitTest, StageResourceBudgetCombinesClassesWithoutDoubleCountingSamplers)
{
    Add(RHIShaderResourceType::eSamplerWithTexture, 3);
    Add(RHIShaderResourceType::eUniformBuffer, 2);
    Add(RHIShaderResourceType::eStorageBuffer, 1);
    Add(RHIShaderResourceType::eImageBuffer, 2);
    Add(RHIShaderResourceType::eInputAttachment, 1);
    Add(RHIShaderResourceType::eSampler, 32);
    Add(RHIShaderResourceType::eAccelerationStructure, 16);
    capacities.slots                             = {1, 1, 8, 1, 1};
    limits.maxPerStageResources                  = 9;
    indexing.maxPerStageUpdateAfterBindResources = 13;
    EXPECT_TRUE(Fits());
    limits.maxPerStageResources = 8;
    EXPECT_FALSE(Fits());
    limits.maxPerStageResources                  = 9;
    indexing.maxPerStageUpdateAfterBindResources = 12;
    EXPECT_FALSE(Fits());
}

TEST_F(VulkanShaderLayoutLimitTest, PushConstantsAndUniformBlockRangeUseDeviceByteLimits)
{
    info.pushConstants.size                                 = 128;
    Add(RHIShaderResourceType::eUniformBuffer, 1).blockSize = limits.maxUniformBufferRange;
    EXPECT_TRUE(Fits());
    info.pushConstants.size = 132;
    EXPECT_FALSE(Fits());
    info.pushConstants.size = 128;
    ++info.SRDTable[1][0].blockSize;
    EXPECT_FALSE(Fits());
}

TEST_F(VulkanShaderLayoutLimitTest, AccelerationStructureLimitsCountBindingsAcrossSetsAndStages)
{
    Add(RHIShaderResourceType::eAccelerationStructure, 16);
    Add(RHIShaderResourceType::eAccelerationStructure, 16, RHIShaderStageFlagBits::eFragment, 2);
    acceleration.maxDescriptorSetAccelerationStructures      = 2;
    acceleration.maxPerStageDescriptorAccelerationStructures = 2;
    EXPECT_TRUE(Fits());
    acceleration.maxDescriptorSetAccelerationStructures = 1;
    EXPECT_FALSE(Fits());
    acceleration.maxDescriptorSetAccelerationStructures      = 2;
    acceleration.maxPerStageDescriptorAccelerationStructures = 1;
    EXPECT_FALSE(Fits());
    info.SRDTable[2][0].stageFlags.Clear();
    info.SRDTable[2][0].stageFlags.SetFlag(RHIShaderStageFlagBits::eCompute);
    EXPECT_TRUE(Fits());
}

TEST_F(VulkanShaderLayoutLimitTest, DescriptorAndHeapSumsCannotWrapAtUint32Max)
{
    const uint32_t maximum                = std::numeric_limits<uint32_t>::max();
    limits.maxDescriptorSetStorageBuffers = limits.maxPerStageDescriptorStorageBuffers = limits.maxPerStageResources = maximum;
    indexing.maxPerStageUpdateAfterBindResources                                                                     = maximum;
    Add(RHIShaderResourceType::eStorageBuffer, maximum);
    EXPECT_TRUE(Fits());
    Add(RHIShaderResourceType::eStorageBuffer, 1, RHIShaderStageFlagBits::eFragment, 2);
    EXPECT_FALSE(Fits());
    info.SRDTable[1].clear();
    info.SRDTable[2].clear();
    limits.maxDescriptorSetSampledImages = limits.maxPerStageDescriptorSampledImages = maximum;
    indexing.maxDescriptorSetUpdateAfterBindSampledImages = indexing.maxPerStageDescriptorUpdateAfterBindSampledImages =
        maximum;
    indexing.maxPerStageUpdateAfterBindResources     = maximum;
    indexing.maxUpdateAfterBindDescriptorsInAllPools = maximum;
    capacities.slots                                 = {1, 1, 1, 1, 1};
    EXPECT_TRUE(Fits());
    EXPECT_TRUE(BindlessHeapsFitLimits(indexing, capacities));
    const RHIBindlessHeapType imageHeaps[] = {RHIBindlessHeapType::eTexture2D, RHIBindlessHeapType::eTextureCube,
                                              RHIBindlessHeapType::eTexture2DArray, RHIBindlessHeapType::eTextureCubeArray};
    for (RHIBindlessHeapType heap : imageHeaps)
    {
        SCOPED_TRACE(ToUnderlying(heap));
        capacities.slots = {1, 1, 1, 1, 1};
        capacities.Set(heap, maximum);
        EXPECT_FALSE(Fits());
        EXPECT_FALSE(BindlessHeapsFitLimits(indexing, capacities));
    }
}

TEST(VulkanBindlessLimitTest, SeparateSamplersDoNotConsumeStageResourceBudget)
{
    RHIBindlessHeapCapacities capacities;
    capacities.slots = {2048, 64, 128, 256, 64};
    VkPhysicalDeviceDescriptorIndexingProperties limits{};
    limits.maxDescriptorSetUpdateAfterBindSampledImages      = 2432;
    limits.maxPerStageDescriptorUpdateAfterBindSampledImages = 2432;
    limits.maxDescriptorSetUpdateAfterBindSamplers           = 128;
    limits.maxPerStageDescriptorUpdateAfterBindSamplers      = 128;
    limits.maxPerStageUpdateAfterBindResources               = 2432;
    limits.maxUpdateAfterBindDescriptorsInAllPools           = 2560;
    EXPECT_TRUE(BindlessHeapsFitLimits(limits, capacities));
    --limits.maxPerStageUpdateAfterBindResources;
    EXPECT_FALSE(BindlessHeapsFitLimits(limits, capacities));
    ++limits.maxPerStageUpdateAfterBindResources;
    --limits.maxUpdateAfterBindDescriptorsInAllPools;
    EXPECT_FALSE(BindlessHeapsFitLimits(limits, capacities));
}

namespace
{
class VulkanBindlessCapacityFitTest : public testing::Test
{
protected:
    VkPhysicalDeviceDescriptorIndexingProperties limits{};
    RHIBindlessHeapCapacities                    requested;
    RHIBindlessHeapCapacities                    fitted;

    void SetUp() override
    {
        limits.maxDescriptorSetUpdateAfterBindSampledImages      = UINT32_MAX;
        limits.maxPerStageDescriptorUpdateAfterBindSampledImages = UINT32_MAX;
        limits.maxPerStageUpdateAfterBindResources               = UINT32_MAX;
        limits.maxDescriptorSetUpdateAfterBindSamplers           = UINT32_MAX;
        limits.maxPerStageDescriptorUpdateAfterBindSamplers      = UINT32_MAX;
        limits.maxUpdateAfterBindDescriptorsInAllPools           = UINT32_MAX;
        fitted.slots                                             = {7, 11, 13, 17, 19};
    }

    void ExpectFit()
    {
        const RHIBindlessHeapCapacities original = requested;
        ASSERT_TRUE(FitBindlessHeapCapacities(limits, requested, fitted));
        EXPECT_EQ(requested.slots, original.slots);
        EXPECT_TRUE(BindlessHeapsFitLimits(limits, fitted));
        for (size_t index = 0; index < fitted.slots.size(); ++index)
        {
            EXPECT_GE(fitted.slots[index], 1u);
            EXPECT_LE(fitted.slots[index], requested.slots[index]);
        }
        RHIBindlessHeapCapacities repeated;
        ASSERT_TRUE(FitBindlessHeapCapacities(limits, requested, repeated));
        EXPECT_EQ(repeated.slots, fitted.slots);
        ASSERT_TRUE(FitBindlessHeapCapacities(limits, repeated, repeated));
        EXPECT_EQ(repeated.slots, fitted.slots);
    }
};
} // namespace

TEST_F(VulkanBindlessCapacityFitTest, RequestsWithinLimitsArePreservedIncludingAliasedOutput)
{
    limits.maxDescriptorSetUpdateAfterBindSampledImages =
        static_cast<uint32_t>(requested.GetImageSlots()) + kBindlessOrdinaryDescriptorReserve;
    limits.maxDescriptorSetUpdateAfterBindSamplers =
        requested.Get(RHIBindlessHeapType::eSampler) + kBindlessOrdinaryDescriptorReserve;
    limits.maxUpdateAfterBindDescriptorsInAllPools =
        static_cast<uint32_t>(requested.GetImageSlots()) + requested.Get(RHIBindlessHeapType::eSampler);
    ExpectFit();
    EXPECT_EQ(fitted.slots, requested.slots);
    EXPECT_TRUE(FitBindlessHeapCapacities(limits, requested, requested));
    EXPECT_EQ(fitted.slots, requested.slots);
}

TEST_F(VulkanBindlessCapacityFitTest, EveryImageLimitReducesAllFourImageHeapsWithoutReducingSamplers)
{
    requested.slots                                                        = {9, 9, 7, 9, 9};
    uint32_t VkPhysicalDeviceDescriptorIndexingProperties::* imageLimits[] = {
        &VkPhysicalDeviceDescriptorIndexingProperties::maxDescriptorSetUpdateAfterBindSampledImages,
        &VkPhysicalDeviceDescriptorIndexingProperties::maxPerStageDescriptorUpdateAfterBindSampledImages,
        &VkPhysicalDeviceDescriptorIndexingProperties::maxPerStageUpdateAfterBindResources};
    for (uint32_t VkPhysicalDeviceDescriptorIndexingProperties::* limit : imageLimits)
    {
        limits.*limit = 20 + kBindlessOrdinaryDescriptorReserve;
        ExpectFit();
        const RHIBindlessHeapCapacities expected{{5, 5, 7, 5, 5}};
        EXPECT_EQ(fitted.slots, expected.slots);
        limits.*limit = UINT32_MAX;
    }
}

TEST_F(VulkanBindlessCapacityFitTest, EachSamplerLimitReducesOnlyTheSamplerHeap)
{
    uint32_t VkPhysicalDeviceDescriptorIndexingProperties::* samplerLimits[] = {
        &VkPhysicalDeviceDescriptorIndexingProperties::maxDescriptorSetUpdateAfterBindSamplers,
        &VkPhysicalDeviceDescriptorIndexingProperties::maxPerStageDescriptorUpdateAfterBindSamplers};
    for (uint32_t VkPhysicalDeviceDescriptorIndexingProperties::* limit : samplerLimits)
    {
        limits.*limit = 3 + kBindlessOrdinaryDescriptorReserve;
        ExpectFit();
        RHIBindlessHeapCapacities expected = requested;
        expected.Set(RHIBindlessHeapType::eSampler, 3);
        EXPECT_EQ(fitted.slots, expected.slots);
        limits.*limit = UINT32_MAX;
    }
}

TEST_F(VulkanBindlessCapacityFitTest, PoolBudgetSharesExtrasAndAssignsRemaindersDeterministically)
{
    requested.slots.fill(5);
    limits.maxUpdateAfterBindDescriptorsInAllPools = 15;
    ExpectFit();
    const RHIBindlessHeapCapacities balanced{{3, 3, 3, 3, 3}};
    EXPECT_EQ(fitted.slots, balanced.slots);
    limits.maxUpdateAfterBindDescriptorsInAllPools = 7;
    ExpectFit();
    const RHIBindlessHeapCapacities remainder{{2, 2, 1, 1, 1}};
    EXPECT_EQ(fitted.slots, remainder.slots);
}

TEST_F(VulkanBindlessCapacityFitTest, CombinedBudgetsPreserveSmallHeapsAndUseTheAvailablePool)
{
    requested.slots                                     = {17, 9, 20, 5, 1};
    limits.maxDescriptorSetUpdateAfterBindSampledImages = 16 + kBindlessOrdinaryDescriptorReserve;
    limits.maxDescriptorSetUpdateAfterBindSamplers      = 8 + kBindlessOrdinaryDescriptorReserve;
    limits.maxUpdateAfterBindDescriptorsInAllPools      = 11;
    ExpectFit();
    EXPECT_EQ(fitted.Get(RHIBindlessHeapType::eTextureCubeArray), 1u);
    EXPECT_EQ(fitted.GetImageSlots() + fitted.Get(RHIBindlessHeapType::eSampler), 11u);
    EXPECT_GT(fitted.Get(RHIBindlessHeapType::eTexture2D), fitted.Get(RHIBindlessHeapType::eTexture2DArray));
    limits.maxDescriptorSetUpdateAfterBindSampledImages = 4 + kBindlessOrdinaryDescriptorReserve;
    limits.maxDescriptorSetUpdateAfterBindSamplers      = 1 + kBindlessOrdinaryDescriptorReserve;
    limits.maxUpdateAfterBindDescriptorsInAllPools      = 5;
    ExpectFit();
    const RHIBindlessHeapCapacities minimum{{1, 1, 1, 1, 1}};
    EXPECT_EQ(fitted.slots, minimum.slots);
}

TEST_F(VulkanBindlessCapacityFitTest, InvalidRequestsAndImpossibleMinimaLeaveOutputUnchanged)
{
    const RHIBindlessHeapCapacities original = fitted;
    for (size_t index = 0; index < requested.slots.size(); ++index)
    {
        const uint32_t saved   = requested.slots[index];
        requested.slots[index] = 0;
        EXPECT_FALSE(FitBindlessHeapCapacities(limits, requested, fitted));
        EXPECT_EQ(fitted.slots, original.slots);
        requested.slots[index] = saved;
    }
    struct LimitCase
    {
        uint32_t VkPhysicalDeviceDescriptorIndexingProperties::* member;
        uint32_t                                                 minimum;
    };
    const LimitCase cases[] = {
        {&VkPhysicalDeviceDescriptorIndexingProperties::maxDescriptorSetUpdateAfterBindSampledImages,
         4 + kBindlessOrdinaryDescriptorReserve},
        {&VkPhysicalDeviceDescriptorIndexingProperties::maxPerStageDescriptorUpdateAfterBindSampledImages,
         4 + kBindlessOrdinaryDescriptorReserve},
        {&VkPhysicalDeviceDescriptorIndexingProperties::maxPerStageUpdateAfterBindResources,
         4 + kBindlessOrdinaryDescriptorReserve},
        {&VkPhysicalDeviceDescriptorIndexingProperties::maxDescriptorSetUpdateAfterBindSamplers,
         1 + kBindlessOrdinaryDescriptorReserve},
        {&VkPhysicalDeviceDescriptorIndexingProperties::maxPerStageDescriptorUpdateAfterBindSamplers,
         1 + kBindlessOrdinaryDescriptorReserve},
        {&VkPhysicalDeviceDescriptorIndexingProperties::maxUpdateAfterBindDescriptorsInAllPools, 5}};
    for (const LimitCase& testCase : cases)
    {
        limits.*testCase.member = testCase.minimum - 1;
        EXPECT_FALSE(FitBindlessHeapCapacities(limits, requested, fitted));
        EXPECT_EQ(fitted.slots, original.slots);
        limits.*testCase.member = UINT32_MAX;
    }
}

TEST_F(VulkanBindlessCapacityFitTest, MaximumRequestsDoNotOverflowScalingOrCombinedCounts)
{
    requested.slots.fill(UINT32_MAX);
    ExpectFit();
    EXPECT_EQ(fitted.GetImageSlots() + fitted.Get(RHIBindlessHeapType::eSampler), uint64_t(UINT32_MAX));
    limits.maxDescriptorSetUpdateAfterBindSampledImages = 7 + kBindlessOrdinaryDescriptorReserve;
    limits.maxDescriptorSetUpdateAfterBindSamplers      = 2 + kBindlessOrdinaryDescriptorReserve;
    limits.maxUpdateAfterBindDescriptorsInAllPools      = 8;
    ExpectFit();
    EXPECT_EQ(fitted.GetImageSlots() + fitted.Get(RHIBindlessHeapType::eSampler), 8u);
}

TEST_F(VulkanBindlessCapacityFitTest, MissingCubeArrayFeatureLeavesTheBudgetToUsableHeaps)
{
    const RHIBindlessHeapCapacities options = RHIOptions::GetInstance().BindlessHeapCapacities();
    EXPECT_EQ(GetBindlessHeapRequest(true).slots, options.slots);
    RHIBindlessHeapCapacities expected = options;
    expected.Set(RHIBindlessHeapType::eTextureCubeArray, 1);
    EXPECT_EQ(GetBindlessHeapRequest(false).slots, expected.slots);
}

TEST_F(VulkanShaderLayoutLimitTest, FittedHeapsLeaveTheReserveForOrdinaryDescriptors)
{
    const uint32_t limit                                       = 1000;
    indexing.maxDescriptorSetUpdateAfterBindSampledImages      = limit;
    indexing.maxPerStageDescriptorUpdateAfterBindSampledImages = limit;
    indexing.maxPerStageUpdateAfterBindResources               = limit;
    indexing.maxDescriptorSetUpdateAfterBindSamplers           = limit;
    indexing.maxPerStageDescriptorUpdateAfterBindSamplers      = limit;
    indexing.maxUpdateAfterBindDescriptorsInAllPools           = UINT32_MAX;
    ASSERT_TRUE(FitBindlessHeapCapacities(indexing, RHIBindlessHeapCapacities{}, capacities));
    EXPECT_EQ(capacities.GetImageSlots(), limit - kBindlessOrdinaryDescriptorReserve);
    Add(RHIShaderResourceType::eSamplerWithTexture, kBindlessOrdinaryDescriptorReserve);
    EXPECT_TRUE(Fits());
    Add(RHIShaderResourceType::eSamplerWithTexture, 1);
    EXPECT_FALSE(Fits());
}
