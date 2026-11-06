RDGGraphicsPassDesc SceneCullRasterPass(const RenderScene& scene,
                                        RHISampler*        sampler,
                                        RHITexture* const  (&targets)[8],
                                        bool               hybrid)
{
    RDGGraphicsPassDesc  pass;
    RHIGfxPipelineStates states{};
    states.rasterizationState.cullMode = RHIPolygonCullMode::eDisabled;
    states.depthStencilState           = RHIGfxPipelineDepthStencilState::Create(true, true, RHIDepthCompareOperator::eLess);
    const uint32_t colorCount          = hybrid ? 7 : 4;
    states.colorBlendState.AddAttachments(colorCount);
    states.dynamicStates.Enable(RHIDynamicState::eScissor, RHIDynamicState::eViewPort);
    pass.SetPipelineStates(states);
    pass.SetRenderArea(0, 0, targets[0]->GetWidth(), targets[0]->GetHeight());
    for (uint32_t index = 0; index < colorCount; ++index)
    {
        pass.AddColorOutput(targets[index]);
    }
    pass.AddDepthStencilOutput(targets[7]);
    pass.BindStorageBuffer("NodeBuffer", scene.GetNodesDataSSBO());
    pass.BindStorageBuffer("MaterialBuffer", scene.GetMaterialsDataSSBO());
    pass.BindStorageBuffer("UVBuffer", scene.GetUVBuffer());
    BindSceneTextureArray(pass, sampler, scene.GetSceneTextureViews(), scene.GetSceneSamplers());
    pass.BindValue("uCameraData", scene.GetCameraUniformData(), sizeof(sg::CameraUniformData));
    pass.BindVertexBuffer(scene.GetVertexBuffer());
    pass.BindIndexBuffer(scene.GetIndexBuffer());
    return pass;
}

void ClearUnusedSceneCullTargets(RenderGraph& graph, RHITexture* const (&targets)[8])
{
    for (uint32_t index = 4; index < 7; ++index)
    {
        graph.AddTransferPass("ClearUnusedReceiverTarget").ClearTexture(targets[index], Color(0));
    }
}

void ReadSceneCullRasterTargets(RenderGraph&      graph,
                                RHISampler*       sampler,
                                RHITexture* const (&targets)[8],
                                RHIBuffer*        packed,
                                RHIBuffer*        readback)
{
    const NameID       names[] = {"normalMap",   "albedoMap", "materialMap", "emissionMap",
                                  "receiverMap", "motionMap", "positionMap", "depthMap"};
    RDGComputePassDesc pass;
    pass.SetShaderProgramName("SceneGBufferReadbackSP");
    for (uint32_t index = 0; index < 8; ++index)
    {
        pass.BindSampledTexture(names[index], sampler, targets[index]->GetDefaultView());
    }
    pass.BindStorageBuffer("GBufferPixels", packed, RDGContentGuarantee::eFullWrite);
    const uint32_t groupsX = (targets[0]->GetWidth() + 7) / 8;
    const uint32_t groupsY = (targets[0]->GetHeight() + 7) / 8;
    graph.AddComputePass(std::move(pass)).RecordPassCommands([groupsX, groupsY](RDGPassCmdEncoder& encoder) {
        encoder.Dispatch(groupsX, groupsY, 1);
    });
    graph.AddTransferPass("ReadCulledGBufferPixels")
        .CopyBuffer(packed, readback, {0, 0, packed->GetRequiredSize()})
        .NeverCull();
}

void ReadSceneCullCommands(RenderGraph& graph, RDGBuffer arguments, uint32_t count, RHIBuffer* output, RHIBuffer* readback)
{
    RDGComputePassDesc copy;
    copy.SetShaderProgramName("SceneIndirectReadbackSP");
    copy.BindStorageBuffer("IndirectCommands", arguments);
    copy.BindStorageBuffer("CopiedCommands", output, RDGContentGuarantee::eFullWrite);
    const uint32_t groups = static_cast<uint32_t>((output->GetRequiredSize() / sizeof(uint32_t) + 63) / 64);
    graph.AddComputePass(std::move(copy)).RecordPassCommands([count, groups](RDGPassCmdEncoder& encoder) {
        encoder.SetPushConstants(count);
        encoder.Dispatch(groups, 1, 1);
    });
    graph.AddTransferPass("ReadProductionCullCommands")
        .CopyBuffer(output, readback, {0, 0, count * sizeof(DrawIndexedIndirectCommand)})
        .NeverCull();
}

TEST_P(ConeVoxelGIIntegrationTest, CulledGBufferMatchesDirectOpaqueMaskedAndHybridReceivers)
{
    ShaderProgramManager::GetInstance().StoreProgram(
        ZEN_NEW() ComputeFileSP(device, "SceneGBufferReadbackSP", "Tests/scene_gbuffer_readback.comp.spv"));
    ShaderProgramManager::GetInstance().StoreProgram(
        ZEN_NEW() ComputeFileSP(device, "SceneIndirectReadbackSP", "Tests/scene_indirect_readback.comp.spv"));
    sg::Scene                       source;
    HeapVector<UniquePtr<sg::Node>> nodes;
    asset::Vertex                   vertices[3]{};
    vertices[0].pos = Vec4(-.4f, -.35f, 0, 1);
    vertices[1].pos = Vec4(.4f, -.35f, 0, 1);
    vertices[2].pos = Vec4(0, .4f, 0, 1);
    for (asset::Vertex& vertex : vertices)
    {
        vertex.normal  = Vec4(0, 0, 1, 0);
        vertex.tangent = Vec4(1, 0, 0, 1);
        vertex.color   = Vec4(1);
    }
    HeapVector<uint32_t> indices;
    for (uint32_t index = 0; index < 15; ++index)
    {
        UniquePtr<sg::Material> material    = MakeUnique<sg::Material>("culling fixture");
        material->index                     = index;
        material->data.baseColorFactor      = Vec4(float(index + 1) / 16, .25f, .5f, index == 13 ? 0.0f : 1.0f);
        material->data.surfaceProperties.y  = index % 2 != 0 ? static_cast<float>(sg::AlphaMode::Mask) : 0.0f;
        material->data.materialProperties.z = index % 3 == 0 ? 1.0f : 0.0f;
        UniquePtr<sg::SubMesh> primitive    = MakeUnique<sg::SubMesh>("culling triangle", 3 * index, 3, 3);
        primitive->SetMaterial(index, material.Get());
        UniquePtr<sg::Mesh> mesh = MakeUnique<sg::Mesh>("culling mesh");
        mesh->AddSubMesh(primitive.Get());
        mesh->SetAABB(Vec3(-.4f, -.35f, 0), Vec3(.4f, .4f, 0));
        UniquePtr<sg::Node> node = MakeUnique<sg::Node>(index, "culling instance");
        node->AddComponent(mesh.Get());
        Vec3 position(1.1f * (int(index % 3) - 1), .9f * (float(index / 3) - 1.5f), -4);
        if (index >= 12)
        {
            position = Vec3(index == 12 ? 30.0f : 0.0f, 0, -3);
        }
        const Mat4 model = glm::translate(Mat4(1), position) * glm::scale(Mat4(1), Vec3(index % 3 == 1 ? -1.0f : 1.0f, 1, 1));
        node->SetData(index, model);
        source.AddRenderableNode(node.Get());
        nodes.push_back(std::move(node));
        source.AddComponent(std::move(mesh));
        source.AddComponent(std::move(primitive));
        source.AddComponent(std::move(material));
        indices.push_back(0);
        indices.push_back(1);
        indices.push_back(2);
    }
    source.GetAABB() = sg::AABB(Vec3(-2, -2, -4), Vec3(31, 2, -3));
    sg::Camera camera(Vec3(0), Vec3(0, 0, -1), 129.0f / 97.0f, 70.0f, .1f, 100.0f);
    SceneData  data{};
    data.pScene      = &source;
    data.pCamera     = &camera;
    data.pVertices   = vertices;
    data.pIndices    = indices.data();
    data.numVertices = 3;
    data.numIndices  = static_cast<uint32_t>(indices.size());
    RenderScene scene(device, data);
    scene.LoadSceneMaterials();
    scene.PrepareBuffers();
    ASSERT_TRUE(scene.SetInstanceEnabled(14, false));
    ASSERT_TRUE(scene.CommitGeometryUpdates());
    const DataFormat formats[] = {DataFormat::eR16G16UNORM,        DataFormat::eR8G8B8A8UNORM, DataFormat::eR8G8B8A8UNORM,
                                  DataFormat::eR16G16B16A16SFloat, DataFormat::eR32G32UInt,    DataFormat::eR32G32B32A32SFloat,
                                  DataFormat::eR32G32B32A32SFloat, DataFormat::eD32SFloat};
    RHITexture*      targets[8]{};
    for (uint32_t index = 0; index < 8; ++index)
    {
        rc::TextureFormat format;
        format.width   = 129;
        format.height  = 97;
        format.depth   = 1;
        format.format  = formats[index];
        targets[index] = index == 7 ? device->CreateTextureDepthStencilRT(format, {}, "CullTestDepth")
                                    : device->CreateTextureColorRT(format, {.copyUsage = true},
                                                                   NameID(fmt::format("CullTestColor{}", index)));
        ASSERT_NE(targets[index], nullptr);
        textures.push_back(targets[index]);
    }
    RHISampler* sampler = device->CreateSampler({});
    ASSERT_NE(sampler, nullptr);
    const uint32_t captureBytes = 129 * 97 * 8 * sizeof(glm::uvec4);
    RHIBuffer*     packed       = Buffer(captureBytes, RHIBufferAllocateType::eGPU);
    RHIBuffer*     readback     = Buffer(captureBytes, RHIBufferAllocateType::eCPURead);
    ASSERT_NE(packed, nullptr);
    ASSERT_NE(readback, nullptr);
    SceneCullingDrawCache drawData;
    RHIBuffer*            commandCopy     = Buffer(15 * sizeof(DrawIndexedIndirectCommand), RHIBufferAllocateType::eGPU);
    RHIBuffer*            commandReadback = Buffer(15 * sizeof(DrawIndexedIndirectCommand), RHIBufferAllocateType::eCPURead);
    ASSERT_NE(commandCopy, nullptr);
    ASSERT_NE(commandReadback, nullptr);
    HybridGIRenderer receiver(device);
    for (uint32_t hybrid = 0; hybrid < 2; ++hybrid)
    {
        SCOPED_TRACE(hybrid);
        receiver.Prepare({nullptr, nullptr, 129, 97}, scene);
        if (hybrid != 0)
        {
            // Retain the prepared model snapshot while committing a new pose so the
            // two paths must carry a nontrivial previousModel and identical motion.
            ASSERT_TRUE(
                scene.SetInstanceTransform(0, glm::translate(Mat4(1), Vec3(.15f, 0, 0)) * scene.GetInstanceTransform(0)));
            ASSERT_TRUE(scene.CommitGeometryUpdates());
        }
        HeapVector<glm::uvec4> reference;
        for (uint32_t indirect = 0; indirect < 3; ++indirect)
        {
            RenderGraph graph("SceneCullingRasterParity");
            ASSERT_TRUE(graph.Begin());
            RDGGraphicsPassDesc pass = SceneCullRasterPass(scene, sampler, targets, hybrid != 0);
            if (hybrid != 0)
            {
                receiver.BindReceiverInputs(pass);
            }
            else
            {
                ClearUnusedSceneCullTargets(graph, targets);
            }
            const HybridGIRenderer* selectedReceiver = hybrid != 0 ? &receiver : nullptr;
            SceneCullingResult      culling;
            if (indirect != 0)
            {
                RDGBuffer arguments;
                culling =
                    AddCulledGBufferPass(*device, drawData, graph, pass, scene, selectedReceiver, indirect == 2, &arguments);
                ASSERT_EQ(culling.status, SceneCullingStatus::eCulled);
                ReadSceneCullCommands(graph, arguments, culling.candidateCount, commandCopy, commandReadback);
            }
            else
            {
                AddGBufferPass(graph, std::move(pass), scene, selectedReceiver);
            }
            ReadSceneCullRasterTargets(graph, sampler, targets, packed, readback);
            ASSERT_TRUE(graph.End());
            ASSERT_TRUE(device->ExecuteRenderGraph(graph)) << graph.GetResult().message;
            device->FlushRHIThread();
            device->WaitForIdle();
            if (indirect != 0)
            {
                const HeapVector<DrawIndexedIndirectCommand> commands =
                    ReadStaticBuffer<DrawIndexedIndirectCommand>(commandReadback);
                ASSERT_EQ(culling.candidateCount, 14u);
                for (uint32_t index = 0; index < culling.candidateCount; ++index)
                {
                    const uint32_t instance = drawData.contents[index].indices.x;
                    EXPECT_EQ(commands[index].firstInstance, index);
                    EXPECT_EQ(commands[index].instanceCount, instance == 12 ? 0u : 1u) << "instance=" << instance;
                }
            }
            const HeapVector<glm::uvec4> pixels        = ReadStaticBuffer<glm::uvec4>(readback);
            uint32_t                     visiblePixels = 0;
            uint32_t                     movingPixels  = 0;
            uint32_t                     colors[12]{};
            for (uint32_t index = 0; index < pixels.size(); index += 8)
            {
                const Vec4 albedo = glm::uintBitsToFloat(pixels[index + 1]);
                if (albedo.w > 0)
                {
                    ++visiblePixels;
                    const int colorIndex = static_cast<int>(std::round(albedo.x * 16)) - 1;
                    EXPECT_GE(colorIndex, 0);
                    EXPECT_LT(colorIndex, 12);
                    if (colorIndex >= 0 && colorIndex < 12)
                    {
                        ++colors[colorIndex];
                        if (hybrid != 0)
                        {
                            EXPECT_EQ(pixels[index + 4].y / 2, static_cast<uint32_t>(colorIndex + 1));
                            const Vec4 motion = glm::uintBitsToFloat(pixels[index + 5]);
                            if (colorIndex == 0 && std::abs(motion.x) > .001f)
                            {
                                ++movingPixels;
                            }
                        }
                    }
                }
            }
            EXPECT_GT(visiblePixels, 100u);
            if (hybrid != 0)
            {
                EXPECT_GT(movingPixels, 0u);
            }
            for (uint32_t index = 0; index < 12; ++index)
            {
                EXPECT_GT(colors[index], 0u) << "missing material/instance " << index;
            }
            if (indirect == 0)
            {
                reference = pixels;
            }
            else
            {
                ASSERT_EQ(pixels.size(), reference.size());
                uint32_t mismatches = 0;
                for (uint32_t index = 0; index < pixels.size(); ++index)
                {
                    if (pixels[index] != reference[index])
                    {
                        ++mismatches;
                    }
                }
                EXPECT_EQ(mismatches, 0u) << "GBuffer or receiver pixels differ from direct drawing";
            }
        }
    }
    // A committed transform must replace both the candidate bounds and its draw metadata.
    ASSERT_TRUE(scene.SetInstanceTransform(12, glm::translate(Mat4(1), Vec3(0, 0, -2))));
    ASSERT_TRUE(scene.CommitGeometryUpdates());
    RenderGraph movedGraph("SceneCullingMovedIntoView");
    ASSERT_TRUE(movedGraph.Begin());
    RDGGraphicsPassDesc      movedPass = SceneCullRasterPass(scene, sampler, targets, false);
    RDGBuffer                movedArguments;
    const SceneCullingResult moved =
        AddCulledGBufferPass(*device, drawData, movedGraph, movedPass, scene, nullptr, false, &movedArguments);
    ASSERT_EQ(moved.status, SceneCullingStatus::eCulled);
    ClearUnusedSceneCullTargets(movedGraph, targets);
    ReadSceneCullCommands(movedGraph, movedArguments, moved.candidateCount, commandCopy, commandReadback);
    ReadSceneCullRasterTargets(movedGraph, sampler, targets, packed, readback);
    ASSERT_TRUE(movedGraph.End());
    ASSERT_TRUE(device->ExecuteRenderGraph(movedGraph)) << movedGraph.GetResult().message;
    device->FlushRHIThread();
    device->WaitForIdle();
    const HeapVector<DrawIndexedIndirectCommand> movedCommands = ReadStaticBuffer<DrawIndexedIndirectCommand>(commandReadback);
    for (uint32_t index = 0; index < moved.candidateCount; ++index)
    {
        EXPECT_EQ(movedCommands[index].instanceCount, 1u);
    }
    const HeapVector<glm::uvec4> movedPixels        = ReadStaticBuffer<glm::uvec4>(readback);
    uint32_t                     movedVisiblePixels = 0;
    for (uint32_t index = 0; index < movedPixels.size(); index += 8)
    {
        const Vec4 albedo = glm::uintBitsToFloat(movedPixels[index + 1]);
        if (albedo.w > 0 && static_cast<uint32_t>(std::round(albedo.x * 16)) == 13)
        {
            ++movedVisiblePixels;
        }
    }
    EXPECT_GT(movedVisiblePixels, 0u);
    for (uint32_t index = 0; index < nodes.size(); ++index)
    {
        ASSERT_TRUE(scene.SetInstanceEnabled(index, false));
    }
    ASSERT_TRUE(scene.CommitGeometryUpdates());
    RenderGraph emptyGraph("SceneCullingAllDisabled");
    ASSERT_TRUE(emptyGraph.Begin());
    RDGGraphicsPassDesc emptyPass = SceneCullRasterPass(scene, sampler, targets, false);
    EXPECT_EQ(AddCulledGBufferPass(*device, drawData, emptyGraph, emptyPass, scene).status, SceneCullingStatus::eEmpty);
    AddGBufferPass(emptyGraph, std::move(emptyPass), scene);
    ClearUnusedSceneCullTargets(emptyGraph, targets);
    ReadSceneCullRasterTargets(emptyGraph, sampler, targets, packed, readback);
    ASSERT_TRUE(emptyGraph.End());
    ASSERT_TRUE(device->ExecuteRenderGraph(emptyGraph)) << emptyGraph.GetResult().message;
    device->FlushRHIThread();
    device->WaitForIdle();
    const HeapVector<glm::uvec4> emptyPixels = ReadStaticBuffer<glm::uvec4>(readback);
    for (uint32_t index = 0; index < emptyPixels.size(); index += 8)
    {
        EXPECT_EQ(glm::uintBitsToFloat(emptyPixels[index + 1]).w, 0.0f);
    }
    receiver.Destroy();
    drawData.Destroy(*device);
}
