void RegisterSceneCullingGraphShaders(RenderDevice* device)
{
    const char* files[][3] = {
        {"GBufferSP", "SceneRenderer/offscreen.vert.spv", "SceneRenderer/offscreen.frag.spv"},
        {"GBufferIndirectSP", "SceneRenderer/offscreen_indirect.vert.spv", "SceneRenderer/offscreen_indirect.frag.spv"}};
    for (const char* const(&entry)[3] : files)
    {
        RHIShaderCreateInfo info;
        info.stageFlags.SetFlags(RHIShaderStageFlagBits::eVertex, RHIShaderStageFlagBits::eFragment);
        info.spirvFileName[ToUnderlying(RHIShaderStage::eVertex)]   = entry[1];
        info.spirvFileName[ToUnderlying(RHIShaderStage::eFragment)] = entry[2];
        reflectedShaderInfos[entry[0]]                              = info;
        CreateTestShaderProgram(device, entry[0]);
    }
    RHIShaderCreateInfo compute;
    compute.stageFlags.SetFlag(RHIShaderStageFlagBits::eCompute);
    compute.spirvFileName[ToUnderlying(RHIShaderStage::eCompute)] = "SceneRenderer/frustum_cull.comp.spv";
    reflectedShaderInfos["SceneFrustumCullSP"]                    = compute;
    CreateTestShaderProgram(device, "SceneFrustumCullSP");
}

void EnableSceneCullingGraphCapabilities(RHIGPUInfo& info)
{
    info.supportDrawIndirectFirstInstance = true;
    info.supportMultiDrawIndirect         = true;
    info.maxDrawIndirectCount             = 2;
    info.maxVertexOutputComponents        = 128;
    info.maxFragmentInputComponents       = 128;
}

uint32_t CountSceneDrawUploads(const TestContext& context, size_t begin)
{
    uint32_t count = 0;
    for (size_t index = begin; index < context.bufferCopyDestinations.size(); ++index)
    {
        if (context.bufferCopyDestinations[index]->GetResourceTag() == "gbuffer_draw_data")
        {
            ++count;
        }
    }
    return count;
}

TEST_F(RenderCoreTest, CulledGBufferGraphBatchesShrinkAcrossFramesAndReuseUnchangedUploads)
{
    RegisterSceneCullingGraphShaders(device);
    EnableSceneCullingGraphCapabilities(rhi->info);
    rhi->info.maxComputeWorkGroupCount = {1, 1, 1};
    TestViewport            viewport;
    HeapVector<RHIBuffer*>  buffers;
    HeapVector<RHITexture*> textures;
    AllocateRendererInputs(0, viewport, buffers, textures);
    sg::Scene    source;
    sg::Material material("culling material");
    sg::SubMesh  primitive("culling primitive", 0, 3, 3);
    primitive.SetMaterial(0, &material);
    sg::Mesh mesh("culling mesh");
    mesh.AddSubMesh(&primitive);
    HeapVector<UniquePtr<sg::Node>> nodes;
    for (uint32_t index = 0; index < 137; ++index)
    {
        UniquePtr<sg::Node> node = MakeUnique<sg::Node>(index, "culling node");
        node->AddComponent(&mesh);
        node->SetData(index, Mat4(1));
        source.AddRenderableNode(node.Get());
        nodes.push_back(std::move(node));
        RenderScene::ShadowCaster caster;
        caster.bounds = sg::AABB(Vec3(-.25f, -.25f, .25f), Vec3(.25f, .25f, .75f));
        sceneInputs.shadowCasters.push_back(caster);
    }
    SceneData data{};
    data.pScene = &source;
    RenderScene              scene(device, data);
    DeferredLightingRenderer lighting(device);
    lighting.Init();
    lighting.SetRenderScene(&scene);
    const uint32_t counts[] = {137, 137, 65, 65, 1, 0};
    for (uint32_t frame = 0; frame < 6; ++frame)
    {
        SCOPED_TRACE(frame);
        rhi->completed = rhi->submitted;
        device->NextFrame();
        source.GetRenderableNodes().resize(counts[frame]);
        rhi->info.supportMultiDrawIndirect      = frame < 3;
        sceneInputs.camera.projViewMatrix[3][0] = .01f * frame;
        const size_t graphicsCopies             = rhi->graphics.bufferCopyDestinations.size();
        const size_t transferCopies             = rhi->transfer.bufferCopyDestinations.size();
        rhi->graphics.indirectDrawCalls.clear();
        rhi->graphics.indexedDraws.clear();
        rhi->graphics.dispatches.clear();
        rhi->graphics.pushConstants.clear();
        RenderGraph& graph = *device->GetCurrentFrameRDG();
        ASSERT_TRUE(graph.Begin());
        lighting.BuildGBufferGraph(RenderView::FromViewport(viewport));
        const SceneCullingResult result = lighting.GetGPUCullingStatus();
        EXPECT_EQ(result.status, frame == 5 ? SceneCullingStatus::eEmpty : SceneCullingStatus::eCulled);
        EXPECT_EQ(result.candidateCount, counts[frame]);
        const bool changed = frame == 0 || frame == 2 || frame == 4;
        EXPECT_EQ(result.uploadedBytes, changed ? counts[frame] * sizeof(GBufferDrawData) : 0u);
        ASSERT_TRUE(graph.End());
        ASSERT_TRUE(device->ExecuteRenderGraph(graph)) << graph.GetResult().message;
        const uint32_t limit   = frame < 3 ? 2u : 1u;
        const uint32_t batches = (counts[frame] + limit - 1) / limit;
        ASSERT_EQ(rhi->graphics.indirectDrawCalls.size(), batches);
        for (uint32_t batch = 0; batch < batches; ++batch)
        {
            const TestContext::IndirectDrawCall& draw = rhi->graphics.indirectDrawCalls[batch];
            EXPECT_EQ(draw.offset, batch * limit * sizeof(DrawIndexedIndirectCommand));
            EXPECT_EQ(draw.count, std::min(limit, counts[frame] - batch * limit));
            EXPECT_EQ(draw.stride, sizeof(DrawIndexedIndirectCommand));
        }
        EXPECT_TRUE(rhi->graphics.indexedDraws.empty());
        const uint32_t dispatches = (counts[frame] + ZEN_SCENE_CULL_GROUP_SIZE - 1) / ZEN_SCENE_CULL_GROUP_SIZE;
        ASSERT_EQ(rhi->graphics.dispatches.size(), dispatches);
        ASSERT_EQ(rhi->graphics.pushConstants.size(), dispatches);
        for (uint32_t dispatch = 0; dispatch < dispatches; ++dispatch)
        {
            EXPECT_EQ(rhi->graphics.dispatches[dispatch].second, glm::uvec3(1));
            ASSERT_EQ(rhi->graphics.pushConstants[dispatch].size(), sizeof(glm::uvec4));
            glm::uvec4 range;
            std::memcpy(&range, rhi->graphics.pushConstants[dispatch].data(), sizeof(range));
            EXPECT_EQ(range.x, dispatch * ZEN_SCENE_CULL_GROUP_SIZE);
            EXPECT_EQ(range.y,
                      std::min(uint32_t(ZEN_SCENE_CULL_GROUP_SIZE), counts[frame] - dispatch * ZEN_SCENE_CULL_GROUP_SIZE));
        }
        EXPECT_EQ(CountSceneDrawUploads(rhi->graphics, graphicsCopies) + CountSceneDrawUploads(rhi->transfer, transferCopies),
                  changed ? 1u : 0u);
    }
    lighting.Destroy();
    for (RHIBuffer* buffer : buffers)
    {
        device->DestroyBuffer(buffer);
    }
    for (RHITexture* texture : textures)
    {
        device->DestroyTexture(texture);
    }
    sceneInputs = {};
}

TEST_F(RenderCoreTest, CulledGBufferGraphReportsCapabilityLimitAndDisabledFallbacks)
{
    RegisterSceneCullingGraphShaders(device);
    EnableSceneCullingGraphCapabilities(rhi->info);
    TestViewport            viewport;
    HeapVector<RHIBuffer*>  buffers;
    HeapVector<RHITexture*> textures;
    AllocateRendererInputs(0, viewport, buffers, textures);
    sg::Scene    source;
    sg::Material material("fallback material");
    sg::SubMesh  primitive("fallback primitive", 0, 3, 3);
    primitive.SetMaterial(0, &material);
    sg::Mesh mesh("fallback mesh");
    mesh.AddSubMesh(&primitive);
    sg::Node node(0, "fallback node");
    node.AddComponent(&mesh);
    source.AddRenderableNode(&node);
    SceneData data{};
    data.pScene = &source;
    RenderScene              scene(device, data);
    DeferredLightingRenderer lighting(device);
    lighting.Init();
    lighting.SetRenderScene(&scene);
    const RHIGPUInfo         supported  = rhi->info;
    const SceneCullingStatus expected[] = {
        SceneCullingStatus::eVaryingLimit, SceneCullingStatus::eVaryingLimit,  SceneCullingStatus::eUnsupportedFirstInstance,
        SceneCullingStatus::eBufferLimit,  SceneCullingStatus::eDispatchLimit, SceneCullingStatus::eDisabled,
        SceneCullingStatus::eUploadFailed, SceneCullingStatus::eCulled};
    // Device checks run first, so unsupported devices never group or count candidates.
    const uint32_t expectedCandidates[] = {0, 0, 0, 2, 1, 0, 1, 1};
    for (uint32_t scenario = 0; scenario < 8; ++scenario)
    {
        SCOPED_TRACE(scenario);
        rhi->info = supported;
        source.GetRenderableNodes().resize(1);
        if (scenario == 0)
        {
            rhi->info.maxVertexOutputComponents = 64;
        }
        else if (scenario == 1)
        {
            rhi->info.maxFragmentInputComponents = 79;
        }
        else if (scenario == 2)
        {
            rhi->info.supportDrawIndirectFirstInstance = false;
        }
        else if (scenario == 3)
        {
            source.AddRenderableNode(&node);
            rhi->info.maxStorageBufferRange = 2 * sizeof(GBufferDrawData) - 1;
        }
        else if (scenario == 4)
        {
            rhi->info.maxComputeWorkGroupInvocations = ZEN_SCENE_CULL_GROUP_SIZE - 1;
        }
        else if (scenario == 6)
        {
            rhi->failBufferCreationAt = rhi->bufferCreations + 1;
        }
        lighting.SetGPUCullingEnabled(scenario != 5);
        rhi->graphics.indirectDrawCalls.clear();
        rhi->graphics.indexedDraws.clear();
        RenderGraph& graph = *device->GetCurrentFrameRDG();
        ASSERT_TRUE(graph.Begin());
        lighting.BuildGBufferGraph(RenderView::FromViewport(viewport));
        EXPECT_EQ(lighting.GetGPUCullingStatus().status, expected[scenario]);
        EXPECT_EQ(lighting.GetGPUCullingStatus().candidateCount, expectedCandidates[scenario]);
        EXPECT_EQ(lighting.GetGPUCullingStatus().uploadedBytes, scenario == 7 ? sizeof(GBufferDrawData) : 0u);
        ASSERT_TRUE(graph.End());
        ASSERT_TRUE(device->ExecuteRenderGraph(graph)) << graph.GetResult().message;
        EXPECT_EQ(rhi->graphics.indirectDrawCalls.size(), scenario == 7 ? 1u : 0u);
        EXPECT_EQ(rhi->graphics.indexedDraws.size(), scenario == 7 ? 0u : scenario == 3 ? 2u : 1u);
        rhi->completed = rhi->submitted;
        device->NextFrame();
    }
    lighting.Destroy();
    rhi->info = supported;
    for (RHIBuffer* buffer : buffers)
    {
        device->DestroyBuffer(buffer);
    }
    for (RHITexture* texture : textures)
    {
        device->DestroyTexture(texture);
    }
    sceneInputs = {};
}
