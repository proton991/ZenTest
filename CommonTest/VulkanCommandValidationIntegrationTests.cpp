#include "VulkanIntegrationFixture.h"
#include "ScopedVulkanCall.h"
#include "Graphics/VulkanRHI/VulkanAccelerationStructure.h"
#include "Graphics/VulkanRHI/VulkanBuffer.h"
#include "Graphics/VulkanRHI/VulkanCommandList.h"
#include "Graphics/VulkanRHI/VulkanDevice.h"
#include <gtest/gtest.h>

namespace
{
using namespace zen;

struct CommandValidationObserver
{
    static inline uint32_t               pushes;
    static inline PFN_vkCmdPushConstants push;
    static VKAPI_ATTR void VKAPI_CALL    Push(VkCommandBuffer    buffer,
                                              VkPipelineLayout   layout,
                                              VkShaderStageFlags stages,
                                              uint32_t           offset,
                                              uint32_t           size,
                                              const void*        data)
    {
        ++pushes;
        push(buffer, layout, stages, offset, size, data);
    }
    static inline uint32_t                                    sizeQueries;
    static inline PFN_vkGetAccelerationStructureBuildSizesKHR sizes;
    static VKAPI_ATTR void VKAPI_CALL                         Sizes(VkDevice                                           device,
                                                                    VkAccelerationStructureBuildTypeKHR                type,
                                                                    const VkAccelerationStructureBuildGeometryInfoKHR* info,
                                                                    const uint32_t*                                    counts,
                                                                    VkAccelerationStructureBuildSizesInfoKHR*          result)
    {
        ++sizeQueries;
        sizes(device, type, info, counts, result);
    }
};

struct ScopedDisabledDrawFeatures
{
    explicit ScopedDisabledDrawFeatures(VulkanDevice* device) :
        features(const_cast<VkPhysicalDeviceFeatures&>(device->GetPhysicalDeviceFeatures())), original(features)
    {
        features.multiDrawIndirect = VK_FALSE;
        features.wideLines         = VK_FALSE;
        features.depthBiasClamp    = VK_FALSE;
    }
    ~ScopedDisabledDrawFeatures()
    {
        features = original;
    }
    VkPhysicalDeviceFeatures& features;
    VkPhysicalDeviceFeatures  original;
};
} // namespace

TEST(VulkanCommandValidationIntegrationTest, OversizedPackedUniformAllocationFailsWithoutGrowing)
{
    test::VulkanSession          session;
    VulkanUniformBufferAllocator allocator;
    allocator.Init(1, 4 * 1024 * 1024, 1);
    EXPECT_FALSE(allocator.Alloc(4 * 1024 * 1024 + 1).IsValid());
    EXPECT_EQ(allocator.GetAllocatedBlockCount(0), 0u);
    EXPECT_EQ(session.rhi.GetLastCreationError().code, RHIErrorCode::eUnsupported);
    EXPECT_STREQ(session.rhi.GetLastCreationError().operation, "Allocate packed uniform buffer");
    EXPECT_TRUE(allocator.Alloc(16).IsValid());
    allocator.Destroy();
}

TEST(VulkanCommandValidationIntegrationTest, PushConstantsValidatePayloadAlignmentAndDeclaredRange)
{
    test::VulkanSession session;
    RHIShaderCreateInfo info;
    test::SetShaderStage(info, RHIShaderStage::eCompute, "binding_push_constants.comp.spv");
    RHIResourcePtr<RHIShader> shader = RHIResourcePtr<RHIShader>::Adopt(session.rhi.CreateShader(info));
    ASSERT_TRUE(shader);
    RHIResourcePtr<RHIPipeline> pipeline =
        RHIResourcePtr<RHIPipeline>::Adopt(session.rhi.CreatePipeline(RHIComputePipelineCreateInfo{shader.Get()}));
    ASSERT_TRUE(pipeline);
    FVulkanCommandListContext context(RHICommandContextType::eGraphics, session.rhi.GetDevice());
    const uint32_t            values[3]{1, 2, 3};
    const uint8_t*            bytes = reinterpret_cast<const uint8_t*>(values);
    struct InvalidRange
    {
        const uint8_t* data;
        uint32_t       size;
        uint32_t       offset;
    };
    const InvalidRange invalid[]{{bytes, 4, 1},   {bytes, 3, 0}, {bytes, 12, 0},        {bytes, 4, 8},
                                 {nullptr, 4, 0}, {bytes, 0, 0}, {bytes, 4, UINT32_MAX}};
    CommandValidationObserver::push   = vkCmdPushConstants;
    CommandValidationObserver::pushes = 0;
    test::ScopedVulkanCall hook(vkCmdPushConstants, CommandValidationObserver::Push);
    for (const InvalidRange& range : invalid)
    {
        context.RHIDiscardRecording();
        context.RHISetPushConstants(pipeline.Get(), MakeVecView(range.data, size_t(range.size)), range.offset);
        EXPECT_EQ(context.GetRecordingError().code, RHIErrorCode::eInvalidArgument);
    }
    EXPECT_EQ(CommandValidationObserver::pushes, 0u);
    context.RHIDiscardRecording();
    context.RHISetPushConstants(pipeline.Get(), MakeVecView(bytes, size_t(4)), 4);
    context.RHISetPushConstants(pipeline.Get(), MakeVecView(bytes, size_t(8)));
    EXPECT_FALSE(context.GetRecordingError().IsFailure());
    EXPECT_EQ(CommandValidationObserver::pushes, 2u);
    context.RHIDiscardRecording();

    test::SetShaderStage(info, RHIShaderStage::eCompute, "descriptor_storage.comp.spv");
    RHIResourcePtr<RHIShader> noPushShader = RHIResourcePtr<RHIShader>::Adopt(session.rhi.CreateShader(info));
    ASSERT_TRUE(noPushShader);
    RHIResourcePtr<RHIPipeline> noPush =
        RHIResourcePtr<RHIPipeline>::Adopt(session.rhi.CreatePipeline(RHIComputePipelineCreateInfo{noPushShader.Get()}));
    ASSERT_TRUE(noPush);
    context.RHISetPushConstants(noPush.Get(), MakeVecView(bytes, size_t(4)));
    EXPECT_EQ(context.GetRecordingError().code, RHIErrorCode::eInvalidArgument);
    EXPECT_EQ(CommandValidationObserver::pushes, 2u);

    RHICommandList* commands = RHICommandList::Create(session.rhi.GetCommandContext(RHICommandContextType::eGraphics));
    commands->SetPushConstants(pipeline.Get(), nullptr, 4, 0);
    commands->Execute();
    EXPECT_EQ(commands->GetContext()->RHIGetRecordingError().code, RHIErrorCode::eInvalidArgument);
    EXPECT_EQ(CommandValidationObserver::pushes, 2u);
    ZEN_DELETE(commands);
}

TEST(VulkanCommandValidationIntegrationTest, UnsupportedIndirectCountAndDynamicFeaturesRejectBeforeDrawing)
{
    test::VulkanSession        session;
    ScopedDisabledDrawFeatures features(session.rhi.GetDevice());
    FVulkanCommandListContext  context(RHICommandContextType::eGraphics, session.rhi.GetDevice());
    context.RHIDrawIndexedIndirect(nullptr, nullptr, DataFormat::eR32UInt, 0, 0, 2, 20);
    EXPECT_EQ(context.GetRecordingError().code, RHIErrorCode::eUnsupported);
    context.RHIDiscardRecording();
    context.RHISetLineWidth(2);
    EXPECT_EQ(context.GetRecordingError().code, RHIErrorCode::eUnsupported);
    context.RHIDiscardRecording();
    context.RHISetDepthBias(0, 1, 0);
    EXPECT_EQ(context.GetRecordingError().code, RHIErrorCode::eUnsupported);
    context.RHIDiscardRecording();
    context.RHISetLineWidth(1);
    context.RHISetDepthBias(0, 0, 0);
    EXPECT_FALSE(context.GetRecordingError().IsFailure());
}

TEST(VulkanCommandValidationIntegrationTest, TLASValidationReusesSizesAndRejectsUnretainedInstances)
{
    test::VulkanSession session;
    if (!session.rhi.QueryGPUInfo().rayQuery.IsUsable())
    {
        GTEST_SKIP() << "Ray query unsupported";
    }
    RHIAccelerationStructureCreateInfo bottomInfo;
    bottomInfo.size = 4096;
    RHIResourcePtr<RHIAccelerationStructure> bottom =
        RHIResourcePtr<RHIAccelerationStructure>::Adopt(session.rhi.CreateAccelerationStructure(bottomInfo));
    RHIResourcePtr<RHIAccelerationStructure> other =
        RHIResourcePtr<RHIAccelerationStructure>::Adopt(session.rhi.CreateAccelerationStructure(bottomInfo));
    ASSERT_TRUE(bottom);
    ASSERT_TRUE(other);
    HeapVector<RHIAccelerationStructureInstance> instances(2);
    instances[0].pStructure = bottom.Get();
    instances[1].pStructure = bottom.Get();
    RHIAccelerationStructureBuildInfo build;
    build.description.type                         = RHIAccelerationStructureType::eTopLevel;
    build.description.instances                    = instances;
    const RHIAccelerationStructureBuildSizes sizes = session.rhi.GetAccelerationStructureBuildSizes(build.description);
    RHIAccelerationStructure*                references[]{bottom.Get()};
    RHIAccelerationStructureCreateInfo       topInfo;
    topInfo.type                 = RHIAccelerationStructureType::eTopLevel;
    topInfo.size                 = sizes.storageSize;
    topInfo.referencedStructures = MakeVecView(static_cast<RHIAccelerationStructure* const*>(references), size_t(1));
    RHIResourcePtr<RHIAccelerationStructure> top =
        RHIResourcePtr<RHIAccelerationStructure>::Adopt(session.rhi.CreateAccelerationStructure(topInfo));
    ASSERT_TRUE(top);
    const uint64_t      alignment = session.rhi.QueryGPUInfo().rayQuery.scratchAlignment;
    RHIBufferCreateInfo scratchInfo;
    scratchInfo.size         = sizes.buildScratchSize + alignment;
    scratchInfo.allocateType = RHIBufferAllocateType::eGPU;
    scratchInfo.usageFlags.SetFlags(RHIBufferUsageFlagBits::eStorageBuffer, RHIBufferUsageFlagBits::eDeviceAddress);
    RHIResourcePtr<RHIBuffer> scratch = RHIResourcePtr<RHIBuffer>::Adopt(session.rhi.CreateBuffer(scratchInfo));
    ASSERT_TRUE(scratch);
    build.pDestination                     = top.Get();
    build.pScratchBuffer                   = scratch.Get();
    build.scratchOffset                    = (alignment - scratch->GetDeviceAddress() % alignment) % alignment;
    VulkanAccelerationStructure* native    = static_cast<VulkanAccelerationStructure*>(top.Get());
    CommandValidationObserver::sizes       = vkGetAccelerationStructureBuildSizesKHR;
    CommandValidationObserver::sizeQueries = 0;
    test::ScopedVulkanCall hook(vkGetAccelerationStructureBuildSizesKHR, CommandValidationObserver::Sizes);
    EXPECT_TRUE(native->ValidateBuild(build));
    EXPECT_TRUE(native->ValidateBuild(build));
    EXPECT_EQ(CommandValidationObserver::sizeQueries, 1u);
    instances[1].pStructure = other.Get();
    EXPECT_FALSE(native->ValidateBuild(build));
    EXPECT_EQ(CommandValidationObserver::sizeQueries, 1u);
    build.description.instances =
        MakeVecView(static_cast<const RHIAccelerationStructureInstance*>(instances.data()), size_t(1));
    EXPECT_TRUE(native->ValidateBuild(build));
    EXPECT_EQ(CommandValidationObserver::sizeQueries, 2u);
}
