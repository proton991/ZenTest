#include "VulkanIntegrationFixture.h"
#include "ScopedVulkanCall.h"
#include "Graphics/VulkanRHI/VulkanDevice.h"
#include "Graphics/VulkanRHI/VulkanDescriptorPool.h"
#include "Graphics/VulkanRHI/VulkanPipeline.h"
#include <gtest/gtest.h>
#include <limits>

namespace
{
using namespace zen;

struct RejectedNativeCreation
{
    static inline uint32_t modules;
    static inline uint32_t setLayouts;
    static inline uint32_t pipelineLayouts;
    static inline uint32_t pipelines;

    static VKAPI_ATTR VkResult VKAPI_CALL Module(VkDevice,
                                                 const VkShaderModuleCreateInfo*,
                                                 const VkAllocationCallbacks*,
                                                 VkShaderModule*)
    {
        ++modules;
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL SetLayout(VkDevice,
                                                    const VkDescriptorSetLayoutCreateInfo*,
                                                    const VkAllocationCallbacks*,
                                                    VkDescriptorSetLayout*)
    {
        ++setLayouts;
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL PipelineLayout(VkDevice,
                                                         const VkPipelineLayoutCreateInfo*,
                                                         const VkAllocationCallbacks*,
                                                         VkPipelineLayout*)
    {
        ++pipelineLayouts;
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL Graphics(VkDevice,
                                                   VkPipelineCache,
                                                   uint32_t,
                                                   const VkGraphicsPipelineCreateInfo*,
                                                   const VkAllocationCallbacks*,
                                                   VkPipeline*)
    {
        ++pipelines;
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
};

void ExpectShaderRejectedBeforeNativeCreation(test::VulkanSession& session, const RHIShaderCreateInfo& info)
{
    RejectedNativeCreation::modules         = 0;
    RejectedNativeCreation::setLayouts      = 0;
    RejectedNativeCreation::pipelineLayouts = 0;
    test::ScopedVulkanCall<PFN_vkCreateShaderModule>        module(vkCreateShaderModule, RejectedNativeCreation::Module);
    test::ScopedVulkanCall<PFN_vkCreateDescriptorSetLayout> setLayout(vkCreateDescriptorSetLayout,
                                                                      RejectedNativeCreation::SetLayout);
    test::ScopedVulkanCall<PFN_vkCreatePipelineLayout>      pipelineLayout(vkCreatePipelineLayout,
                                                                           RejectedNativeCreation::PipelineLayout);
    RHIShader*                                              shader = session.rhi.CreateShader(info);
    EXPECT_EQ(shader, nullptr);
    EXPECT_EQ(RejectedNativeCreation::modules, 0u);
    EXPECT_EQ(RejectedNativeCreation::setLayouts, 0u);
    EXPECT_EQ(RejectedNativeCreation::pipelineLayouts, 0u);
    if (shader != nullptr)
    {
        session.rhi.DestroyShader(shader);
    }
}

class ScopedDisabledFeature
{
public:
    ScopedDisabledFeature(VulkanDevice* device, VkBool32 VkPhysicalDeviceFeatures::* member) :
        value(const_cast<VkPhysicalDeviceFeatures&>(device->GetPhysicalDeviceFeatures()).*member), previous(value)
    {
        value = VK_FALSE;
    }
    ~ScopedDisabledFeature()
    {
        value = previous;
    }

private:
    VkBool32& value;
    VkBool32  previous;
};

void ExpectGraphicsRejectedBeforeNativeCreation(test::VulkanSession& session, const RHIGfxPipelineCreateInfo& info)
{
    RejectedNativeCreation::pipelines = 0;
    test::ScopedVulkanCall<PFN_vkCreateGraphicsPipelines> pipeline(vkCreateGraphicsPipelines, RejectedNativeCreation::Graphics);
    RHIPipeline*                                          rejected = session.rhi.CreatePipeline(info);
    EXPECT_EQ(rejected, nullptr);
    EXPECT_EQ(RejectedNativeCreation::pipelines, 0u);
    if (rejected != nullptr)
    {
        session.rhi.DestroyPipeline(rejected);
    }
}

class ScopedFragmentDescriptors
{
public:
    ScopedFragmentDescriptors(RHIShader* shader, uint32_t resourceCount) :
        table(*const_cast<RHIShaderResourceDescriptorTable*>(shader->GetSRDTable())), previous(table)
    {
        table.clear();
        table.resize(2);
        RHIShaderResourceDescriptor descriptor;
        descriptor.type = RHIShaderResourceType::eTexture;
        descriptor.set  = 1;
        descriptor.stageFlags.SetFlag(RHIShaderStageFlagBits::eFragment);
        table[1].push_back(descriptor);
        SetCount(resourceCount);
    }

    ~ScopedFragmentDescriptors()
    {
        table = std::move(previous);
    }

    void SetCount(uint32_t resourceCount)
    {
        table[1][0].arraySize = resourceCount;
    }

private:
    RHIShaderResourceDescriptorTable& table;
    RHIShaderResourceDescriptorTable  previous;
};
} // namespace

TEST(VulkanShaderLayoutIntegrationTest, TessellationCapabilityAndStagesAreExplicitlyUnsupported)
{
    test::VulkanSession session;
    EXPECT_FALSE(session.rhi.QueryGPUInfo().supportTessellationShader);
    EXPECT_EQ(session.rhi.GetDevice()->GetPhysicalDeviceFeatures().tessellationShader, VK_FALSE);
    RHIShaderCreateInfo control;
    test::SetShaderStage(control, RHIShaderStage::eTesselationControl, "unsupported_tessellation.tesc.spv");
    ExpectShaderRejectedBeforeNativeCreation(session, control);
    RHIShaderCreateInfo evaluation;
    test::SetShaderStage(evaluation, RHIShaderStage::eTesselationEvaluation, "unsupported_tessellation.tese.spv");
    ExpectShaderRejectedBeforeNativeCreation(session, evaluation);
}

TEST(VulkanShaderLayoutIntegrationTest, PatchTopologyIsRejectedBeforeNativeGraphicsCreation)
{
    test::VulkanSession session;
    RHIShaderCreateInfo shaderInfo;
    test::SetShaderStage(shaderInfo, RHIShaderStage::eVertex, "pipeline.vert.spv");
    test::SetShaderStage(shaderInfo, RHIShaderStage::eFragment, "pipeline.frag.spv");
    RHIShader* shader = session.rhi.CreateShader(shaderInfo);
    ASSERT_NE(shader, nullptr);
    RHIRenderingLayout layout;
    layout.SetRenderArea(0, 0, 8, 8);
    layout.numColorRenderTargets        = 1;
    layout.colorRenderTargets[0].format = DataFormat::eR8G8B8A8UNORM;
    RHIGfxPipelineCreateInfo info;
    info.pShader                      = shader;
    info.pRenderingLayout             = &layout;
    info.states.primitiveType         = RHIDrawPrimitiveType::ePatchList;
    RejectedNativeCreation::pipelines = 0;
    {
        test::ScopedVulkanCall<PFN_vkCreateGraphicsPipelines> pipeline(vkCreateGraphicsPipelines,
                                                                       RejectedNativeCreation::Graphics);
        RHIPipeline*                                          rejected = session.rhi.CreatePipeline(info);
        EXPECT_EQ(rejected, nullptr);
        EXPECT_EQ(RejectedNativeCreation::pipelines, 0u);
        if (rejected != nullptr)
        {
            session.rhi.DestroyPipeline(rejected);
        }
    }
    info.states.primitiveType = RHIDrawPrimitiveType::eTriangleList;
    RHIPipeline* valid        = session.rhi.CreatePipeline(info);
    EXPECT_NE(valid, nullptr);
    if (valid != nullptr)
    {
        session.rhi.DestroyPipeline(valid);
    }
    session.rhi.DestroyShader(shader);
}

TEST(VulkanShaderLayoutIntegrationTest, DeviceLayoutLimitsRejectSpecializationMetadataBeforeNativeCreation)
{
    test::VulkanSession session;
    RHIShaderCreateInfo source;
    test::SetShaderStage(source, RHIShaderStage::eCompute, "pipeline_specialization.comp.spv");
    RHIShader* original = session.rhi.CreateShader(source);
    ASSERT_NE(original, nullptr);
    const RHIShaderCreateInfo reusable = original->GetCreateInfo();
    ASSERT_TRUE(reusable.reflection.has_value());
    const VkPhysicalDeviceLimits limits = session.rhi.GetDevice()->GetPhysicalDeviceProperties().limits;

    RHIShaderCreateInfo pushConstants   = reusable;
    ASSERT_LE(limits.maxPushConstantsSize, std::numeric_limits<uint32_t>::max() - 4);
    pushConstants.reflection->pushConstants.size = limits.maxPushConstantsSize + 4;
    pushConstants.reflection->pushConstants.stageFlags.SetFlag(RHIShaderStageFlagBits::eCompute);
    ExpectShaderRejectedBeforeNativeCreation(session, pushConstants);

    struct DescriptorCase
    {
        RHIShaderResourceType type;
        uint32_t              limit;
    };
    const DescriptorCase cases[] = {
        {RHIShaderResourceType::eTexture,
         std::min(limits.maxDescriptorSetSampledImages, limits.maxPerStageDescriptorSampledImages)},
        {RHIShaderResourceType::eImage,
         std::min(limits.maxDescriptorSetStorageImages, limits.maxPerStageDescriptorStorageImages)},
        {RHIShaderResourceType::eStorageBuffer,
         std::min(limits.maxDescriptorSetStorageBuffers, limits.maxPerStageDescriptorStorageBuffers)},
        {RHIShaderResourceType::eInputAttachment,
         std::min(limits.maxDescriptorSetInputAttachments, limits.maxPerStageDescriptorInputAttachments)}};
    for (const DescriptorCase& testCase : cases)
    {
        SCOPED_TRACE(ToUnderlying(testCase.type));
        ASSERT_LT(testCase.limit, std::numeric_limits<uint32_t>::max());
        RHIShaderCreateInfo          overLimit  = reusable;
        RHIShaderResourceDescriptor& descriptor = overLimit.reflection->SRDTable[1][0];
        descriptor.type                         = testCase.type;
        descriptor.arraySize                    = testCase.limit + 1;
        ExpectShaderRejectedBeforeNativeCreation(session, overLimit);
    }

    // Reusing unchanged reflection remains valid after all the rejected attempts.
    RHIShader* valid = session.rhi.CreateShader(reusable);
    EXPECT_NE(valid, nullptr);
    if (valid != nullptr)
    {
        session.rhi.DestroyShader(valid);
    }
    session.rhi.DestroyShader(original);
}

TEST(VulkanShaderLayoutIntegrationTest, FragmentColorAttachmentCountsBeforeNativeGraphicsCreation)
{
    test::VulkanSession session;
    RHIShaderCreateInfo shaderInfo;
    test::SetShaderStage(shaderInfo, RHIShaderStage::eVertex, "pipeline.vert.spv");
    test::SetShaderStage(shaderInfo, RHIShaderStage::eFragment, "pipeline.frag.spv");
    RHIShader* shader = session.rhi.CreateShader(shaderInfo);
    ASSERT_NE(shader, nullptr);
    const VulkanDevice*              device       = session.rhi.GetDevice();
    const RHIBindlessHeapCapacities& capacities   = session.rhi.GetBindlessDescriptorPoolManager()->GetCapacities();
    const uint64_t                   globalImages = capacities.GetImageSlots();
    const uint32_t aggregateLimit = device->GetDescriptorIndexingProperties().maxPerStageUpdateAfterBindResources;
    ASSERT_GT(aggregateLimit, globalImages);
    const uint32_t resourceBudget = std::min(device->GetPhysicalDeviceProperties().limits.maxPerStageResources,
                                             static_cast<uint32_t>(aggregateLimit - globalImages));
    ASSERT_GT(resourceBudget, 0u);
    RHIRenderingLayout layout;
    layout.SetRenderArea(0, 0, 8, 8);
    layout.numColorRenderTargets        = 1;
    layout.colorRenderTargets[0].format = DataFormat::eR8G8B8A8UNORM;
    RHIGfxPipelineCreateInfo info;
    info.pShader          = shader;
    info.pRenderingLayout = &layout;

    // Exercise device-dependent count boundaries without allocating a huge native shader layout.
    // The native shader stays valid; restore its reflected table before destruction.
    {
        ScopedFragmentDescriptors descriptors(shader, resourceBudget);
        RejectedNativeCreation::pipelines = 0;
        {
            test::ScopedVulkanCall<PFN_vkCreateGraphicsPipelines> pipeline(vkCreateGraphicsPipelines,
                                                                           RejectedNativeCreation::Graphics);
            RHIPipeline*                                          rejected = session.rhi.CreatePipeline(info);
            EXPECT_EQ(rejected, nullptr);
            EXPECT_EQ(RejectedNativeCreation::pipelines, 0u);
            if (rejected != nullptr)
            {
                session.rhi.DestroyPipeline(rejected);
            }
        }
        descriptors.SetCount(resourceBudget - 1);
        RHIPipeline* boundary = session.rhi.CreatePipeline(info);
        EXPECT_NE(boundary, nullptr);
        if (boundary != nullptr)
        {
            session.rhi.DestroyPipeline(boundary);
        }
    }
    session.rhi.DestroyShader(shader);
}

TEST(VulkanShaderLayoutIntegrationTest, SpecializationTypeMismatchIsRejectedBeforeNativeCreation)
{
    test::VulkanSession session;
    RHIShaderCreateInfo source;
    test::SetShaderStage(source, RHIShaderStage::eCompute, "pipeline_specialization.comp.spv");
    RHIShader* shader = session.rhi.CreateShader(source);
    ASSERT_NE(shader, nullptr);
    const RHIShaderCreateInfo reusable = shader->GetCreateInfo();
    struct OverrideCase
    {
        uint32_t                     id;
        RHIShaderSpecializationValue value;
    };
    const OverrideCase cases[] = {{0, 1}, {1, true}, {2, 1}, {2, RHIShaderSpecializationValue{}}};
    for (const OverrideCase& testCase : cases)
    {
        SCOPED_TRACE(testCase.id);
        RHIShaderCreateInfo invalid                  = reusable;
        invalid.specializationConstants[testCase.id] = testCase.value;
        ExpectShaderRejectedBeforeNativeCreation(session, invalid);
    }
    RHIShaderCreateInfo valid     = reusable;
    valid.specializationConstants = {{0, false}, {1, -19}, {2, 4.5f}};
    RHIShader* specialized        = session.rhi.CreateShader(valid);
    EXPECT_NE(specialized, nullptr);
    if (specialized != nullptr)
    {
        session.rhi.DestroyShader(specialized);
    }
    session.rhi.DestroyShader(shader);
}

TEST(VulkanShaderLayoutIntegrationTest, DisabledGraphicsFeaturesAreRejectedBeforeNativeCreation)
{
    test::VulkanSession session;
    RHIShaderCreateInfo shaderInfo;
    test::SetShaderStage(shaderInfo, RHIShaderStage::eVertex, "pipeline.vert.spv");
    test::SetShaderStage(shaderInfo, RHIShaderStage::eFragment, "pipeline.frag.spv");
    RHIShader* shader = session.rhi.CreateShader(shaderInfo);
    ASSERT_NE(shader, nullptr);
    struct FeatureCase
    {
        const char* name;
        VkBool32 VkPhysicalDeviceFeatures::* feature;
        RHIGfxPipelineStates                 states;
        uint32_t                             colorCount{1};
    };
    FeatureCase cases[]                          = {{"wireframe", &VkPhysicalDeviceFeatures::fillModeNonSolid},
                                                    {"depth clamp", &VkPhysicalDeviceFeatures::depthClamp},
                                                    {"depth bounds", &VkPhysicalDeviceFeatures::depthBounds},
                                                    {"alpha to one", &VkPhysicalDeviceFeatures::alphaToOne},
                                                    {"sample shading", &VkPhysicalDeviceFeatures::sampleRateShading},
                                                    {"logic operation", &VkPhysicalDeviceFeatures::logicOp},
                                                    {"depth bias clamp", &VkPhysicalDeviceFeatures::depthBiasClamp},
                                                    {"wide lines", &VkPhysicalDeviceFeatures::wideLines},
                                                    {"independent blending", &VkPhysicalDeviceFeatures::independentBlend},
                                                    {"dual source blending", &VkPhysicalDeviceFeatures::dualSrcBlend},
                                                    {"adjacency topology", &VkPhysicalDeviceFeatures::geometryShader}};
    cases[0].states.rasterizationState.wireframe = true;
    cases[1].states.rasterizationState.enableDepthClamp     = true;
    cases[2].states.depthStencilState.enableDepthBoundsTest = true;
    cases[3].states.multiSampleState.enableAlphaToOne       = true;
    cases[4].states.multiSampleState.enableSampleShading    = true;
    cases[5].states.colorBlendState.enableLogicOp           = true;
    cases[6].states.rasterizationState.enableDepthBias      = true;
    cases[6].states.rasterizationState.depthBiasClamp       = 1.0f;
    cases[7].states.rasterizationState.lineWidth            = 2.0f;
    cases[8].colorCount                                     = 2;
    cases[8].states.colorBlendState.AddAttachment(); // The other native attachment is zero/write-disabled.
    cases[9].states.colorBlendState.AddAttachment();
    cases[9].states.colorBlendState.attachments[0].enableBlend         = true;
    cases[9].states.colorBlendState.attachments[0].srcColorBlendFactor = RHIBlendFactor::eSrc1Color;
    cases[10].states.primitiveType                                     = RHIDrawPrimitiveType::eTriangleListWithAdjacency;
    RHIRenderingLayout layout;
    layout.SetRenderArea(0, 0, 1, 1);
    layout.colorRenderTargets[0].format = layout.colorRenderTargets[1].format = DataFormat::eR8G8B8A8UNORM;
    RHIGfxPipelineCreateInfo info;
    info.pShader          = shader;
    info.pRenderingLayout = &layout;
    for (const FeatureCase& testCase : cases)
    {
        SCOPED_TRACE(testCase.name);
        ScopedDisabledFeature disabled(session.rhi.GetDevice(), testCase.feature);
        layout.numColorRenderTargets = testCase.colorCount;
        info.states                  = testCase.states;
        ExpectGraphicsRejectedBeforeNativeCreation(session, info);
    }
    // Unused static values are legal when their corresponding state is dynamic.
    info.states                                    = {};
    info.states.rasterizationState.lineWidth       = 2.0f;
    info.states.rasterizationState.enableDepthBias = true;
    info.states.rasterizationState.depthBiasClamp  = 1.0f;
    info.states.dynamicStates.Enable(RHIDynamicState::eLineWidth, RHIDynamicState::eDepthBias);
    layout.numColorRenderTargets = 2; // Identical default attachments do not require independentBlend.
    {
        ScopedDisabledFeature wideLines(session.rhi.GetDevice(), &VkPhysicalDeviceFeatures::wideLines);
        ScopedDisabledFeature depthBiasClamp(session.rhi.GetDevice(), &VkPhysicalDeviceFeatures::depthBiasClamp);
        ScopedDisabledFeature independentBlend(session.rhi.GetDevice(), &VkPhysicalDeviceFeatures::independentBlend);
        RHIPipeline*          valid = session.rhi.CreatePipeline(info);
        EXPECT_NE(valid, nullptr);
        if (valid != nullptr)
        {
            session.rhi.DestroyPipeline(valid);
        }
    }
    session.rhi.DestroyShader(shader);
}

TEST(VulkanShaderLayoutIntegrationTest, DualSourceBlendingIsEnabledWithinItsColorTargetLimit)
{
    test::VulkanSession      session;
    VulkanDevice*            device = session.rhi.GetDevice();
    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(device->GetPhysicalDeviceHandle(), &supported);
    EXPECT_EQ(device->GetPhysicalDeviceFeatures().dualSrcBlend, supported.dualSrcBlend);
    const uint32_t limit = device->GetPhysicalDeviceProperties().limits.maxFragmentDualSrcAttachments;
    if (supported.dualSrcBlend == VK_FALSE || limit >= MAX_NUM_COLOR_ATTACHMENTS)
    {
        GTEST_SKIP() << "The device cannot exceed its dual-source color target limit";
    }
    RHIShaderCreateInfo shaderInfo;
    test::SetShaderStage(shaderInfo, RHIShaderStage::eVertex, "pipeline.vert.spv");
    test::SetShaderStage(shaderInfo, RHIShaderStage::eFragment, "pipeline_dual_source.frag.spv");
    RHIShader* shader = session.rhi.CreateShader(shaderInfo);
    ASSERT_NE(shader, nullptr);
    RHIRenderingLayout layout;
    layout.SetRenderArea(0, 0, 1, 1);
    layout.numColorRenderTargets = limit + 1;
    for (uint32_t index = 0; index < layout.numColorRenderTargets; ++index)
    {
        layout.colorRenderTargets[index].format = DataFormat::eR8G8B8A8UNORM;
    }
    RHIGfxPipelineCreateInfo info;
    info.pShader          = shader;
    info.pRenderingLayout = &layout;
    info.states.colorBlendState.AddAttachment();
    info.states.colorBlendState.attachments[0].enableBlend         = true;
    info.states.colorBlendState.attachments[0].srcColorBlendFactor = RHIBlendFactor::eSrc1Color;
    ExpectGraphicsRejectedBeforeNativeCreation(session, info);
    session.rhi.DestroyShader(shader);
}

TEST(VulkanShaderLayoutIntegrationTest, GraphicsPipelineRequiresVertexAndExcludesComputeStages)
{
    test::VulkanSession session;
    RHIRenderingLayout  layout;
    layout.SetRenderArea(0, 0, 1, 1);
    layout.numColorRenderTargets        = 1;
    layout.colorRenderTargets[0].format = DataFormat::eR8G8B8A8UNORM;
    for (uint32_t stageCase = 0; stageCase < 3; ++stageCase)
    {
        SCOPED_TRACE(stageCase);
        RHIShaderCreateInfo shaderInfo;
        if (stageCase == 0)
        {
            test::SetShaderStage(shaderInfo, RHIShaderStage::eFragment, "pipeline.frag.spv");
        }
        else
        {
            test::SetShaderStage(shaderInfo, RHIShaderStage::eCompute, "pipeline_specialization.comp.spv");
            if (stageCase == 2)
            {
                test::SetShaderStage(shaderInfo, RHIShaderStage::eVertex, "pipeline.vert.spv");
            }
        }
        RHIShader* shader = session.rhi.CreateShader(shaderInfo);
        ASSERT_NE(shader, nullptr);
        RHIGfxPipelineCreateInfo info;
        info.pShader          = shader;
        info.pRenderingLayout = &layout;
        ExpectGraphicsRejectedBeforeNativeCreation(session, info);
        session.rhi.DestroyShader(shader);
    }
}
