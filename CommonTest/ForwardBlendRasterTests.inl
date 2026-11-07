class ForwardBlendDrawCounters
{
public:
    ForwardBlendDrawCounters() : m_previous(RHIOptions::GetInstance().ExecutionCountersEnabled())
    {
        RHIOptions::GetInstance().SetExecutionCountersEnabled(true);
    }

    ~ForwardBlendDrawCounters()
    {
        RHIOptions::GetInstance().SetExecutionCountersEnabled(m_previous);
    }

private:
    bool m_previous;
};

struct ForwardBlendSceneFixture
{
    RenderDevice&                   device;
    sg::Scene                       source;
    HeapVector<UniquePtr<sg::Node>> nodes;
    HeapVector<asset::Vertex>       vertices;
    HeapVector<uint32_t>            indices;
    sg::Camera                      camera{Vec3(0), Vec3(0, 0, -1), 1.0f, 60.0f, .1f, 10.0f};
    UniquePtr<RenderScene>          scene;

    explicit ForwardBlendSceneFixture(RenderDevice& renderDevice) : device(renderDevice) {}

    ~ForwardBlendSceneFixture()
    {
        device.WaitForPreviousFrames();
        device.GetRendererServer()->SetRenderScene(nullptr);
        if (scene)
        {
            scene->Destroy();
        }
    }

    bool Init(uint32_t scenario, bool withVariant = false)
    {
        // Depth order alternates between nodes A/B/A/B. Vertex colors make any
        // change to that order visible even though every triangle shares one material.
        const Vec4 colors[]  = {Vec4(1, 0, 0, .5f), Vec4(0, 1, 0, .5f), Vec4(0, 0, 1, .5f), Vec4(1, 1, 0, .5f)};
        const Vec2 corners[] = {Vec2(-.75f, -.75f), Vec2(.75f, -.75f), Vec2(0, .75f)};
        for (uint32_t triangle = 0; triangle < 4; ++triangle)
        {
            for (const Vec2& corner : corners)
            {
                asset::Vertex vertex{};
                vertex.pos = Vec4(corner, -4.0f + .5f * triangle, 1);
                if (scenario == 0 && (triangle & 1u) != 0)
                {
                    // Countertranslate B's local positions: this reference has
                    // different node data, but exactly the same world positions.
                    vertex.pos.x -= 2.0f;
                }
                vertex.normal  = Vec4(0, 0, 1, 0);
                vertex.tangent = Vec4(1, 0, 0, 1);
                vertex.color   = colors[triangle];
                vertices.push_back(vertex);
            }
        }
        UniquePtr<sg::Material> material    = MakeUnique<sg::Material>("forward blend parity");
        material->index                     = 0;
        material->data.surfaceProperties.y  = static_cast<float>(sg::AlphaMode::Blend);
        material->data.materialProperties.y = 1.0f;
        material->data.materialProperties.z = 1.0f;
        sg::Material* materialPointer       = material.Get();
        source.AddComponent(std::move(material));
        if (withVariant)
        {
            UniquePtr<sg::Material> variant = MakeUnique<sg::Material>("opaque variant");
            variant->index                  = 1;
            source.AddComponent(std::move(variant));
            source.GetAssetData().materialVariants.push_back("opaque");
        }
        for (uint32_t instance = 0; instance < 2; ++instance)
        {
            const uint32_t firstIndex = static_cast<uint32_t>(indices.size());
            sg::AABB       bounds;
            for (uint32_t triangle = instance; triangle < 4; triangle += 2)
            {
                for (uint32_t corner = 0; corner < 3; ++corner)
                {
                    const uint32_t vertex = triangle * 3 + corner;
                    indices.push_back(vertex);
                    bounds.SetMin(Vec3(vertices[vertex].pos));
                    bounds.SetMax(Vec3(vertices[vertex].pos));
                }
            }
            UniquePtr<sg::SubMesh> primitive = MakeUnique<sg::SubMesh>("interleaved blend triangles", firstIndex, 6, 6);
            primitive->SetMaterial(0, materialPointer);
            primitive->SetBounds(bounds);
            if (withVariant)
            {
                primitive->assetMesh      = instance;
                primitive->assetPrimitive = 0;
                source.GetAssetData().variantPrimitives.push_back({instance, 0, {1}});
            }
            UniquePtr<sg::Mesh> mesh = MakeUnique<sg::Mesh>("forward blend mesh");
            mesh->AddSubMesh(primitive.Get());
            mesh->SetAABB(bounds.GetMin(), bounds.GetMax());
            UniquePtr<sg::Node> node = MakeUnique<sg::Node>(instance, "forward blend node");
            node->AddComponent(mesh.Get());
            const float translation = instance == 0 || scenario == 1 ? 0.0f : scenario == 0 ? 2.0f : .5f;
            node->SetData(instance, glm::translate(Mat4(1), Vec3(translation, 0, 0)));
            source.AddRenderableNode(node.Get());
            nodes.push_back(std::move(node));
            source.AddComponent(std::move(mesh));
            source.AddComponent(std::move(primitive));
        }
        SceneData data{};
        data.pScene      = &source;
        data.pCamera     = &camera;
        data.pVertices   = vertices.data();
        data.numVertices = static_cast<uint32_t>(vertices.size());
        data.pIndices    = indices.data();
        data.numIndices  = static_cast<uint32_t>(indices.size());
        scene            = MakeUnique<RenderScene>(&device, data);
        const bool valid = scene->Init() && scene->SetEnvironmentLighting(0, 0, false, false);
        if (valid)
        {
            device.GetRendererServer()->SetRenderScene(scene.Get());
        }
        return valid;
    }
};

void ReadForwardBlendPixels(RenderDevice& device, RHITexture* color, RHIBuffer* packed, RHIBuffer* readback)
{
    RenderGraph graph("ForwardBlendParityReadback");
    ASSERT_TRUE(graph.Begin());
    RDGComputePassDesc pass;
    pass.SetShaderProgramName("CaptureFrameSP");
    pass.BindSampledTexture("sourceColor", device.CreateSampler(RHISamplerCreateInfo::CreateLinearRepeat()),
                            color->GetDefaultView());
    pass.BindStorageBuffer("packedColor", packed, RDGContentGuarantee::eFullWrite);
    const glm::uvec2 groups((color->GetWidth() + 7) / 8, (color->GetHeight() + 7) / 8);
    graph.AddComputePass(std::move(pass)).RecordPassCommands([groups](RDGPassCmdEncoder& encoder) {
        encoder.Dispatch(groups.x, groups.y, 1);
    });
    graph.AddTransferPass("ReadForwardBlendPixels").CopyBuffer(packed, readback, {0, 0, packed->GetRequiredSize()}).NeverCull();
    ASSERT_TRUE(graph.End()) << graph.GetResult().message;
    ASSERT_TRUE(device.ExecuteRenderGraph(graph)) << graph.GetResult().message;
    device.WaitForPreviousFrames();
}

TEST_P(ConeVoxelGIIntegrationTest, EquivalentBlendNodesPreserveSortedPixelsAndReduceNativeDraws)
{
    ForwardBlendDrawCounters counters;
    device->InitializeRendererServer();
    RendererServer& server = *device->GetRendererServer();
    server.SetRenderOption(RenderOption::ePBR);
    rc::TextureFormat format;
    format.width = format.height = 96;
    format.depth                 = 1;
    format.format                = DataFormat::eR8G8B8A8UNORM;
    RHITexture* color            = device->CreateTextureColorRT(format, {.copyUsage = true}, "ForwardBlendParityColor");
    format.format                = DataFormat::eD32SFloat;
    RHITexture* depth            = device->CreateTextureDepthStencilRT(format, {}, "ForwardBlendParityDepth");
    textures.push_back(color);
    textures.push_back(depth);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(depth, nullptr);
    const RenderView view{color, depth, 96, 96};
    RHIBuffer*       packed   = Buffer(96 * 96 * sizeof(uint32_t), RHIBufferAllocateType::eGPU);
    RHIBuffer*       readback = Buffer(96 * 96 * sizeof(uint32_t), RHIBufferAllocateType::eCPURead);
    ASSERT_NE(packed, nullptr);
    ASSERT_NE(readback, nullptr);
    HeapVector<uint32_t> reference;
    uint64_t             referenceDraws = 0;
    for (uint32_t scenario = 0; scenario < 3; ++scenario)
    {
        SCOPED_TRACE(scenario);
        ForwardBlendSceneFixture fixture(*device);
        ASSERT_TRUE(fixture.Init(scenario));
        // Retire environment preprocessing before measuring the repeated scene frame.
        ASSERT_EQ(server.DispatchRenderWorkloads(view), RenderFrameResult::eSubmitted);
        device->WaitForPreviousFrames();
        device->NextFrame();
        const uint64_t before = device->GetRHIThreadMetrics().native.draws;
        ASSERT_EQ(server.DispatchRenderWorkloads(view), RenderFrameResult::eSubmitted);
        device->WaitForPreviousFrames();
        const uint64_t draws = device->GetRHIThreadMetrics().native.draws - before;
        ReadForwardBlendPixels(*device, color, packed, readback);
        ASSERT_FALSE(HasFatalFailure());
        const HeapVector<uint32_t> pixels = ReadStaticBuffer<uint32_t>(readback);
        ASSERT_EQ(pixels.size(), 96u * 96u);
        if (scenario == 0)
        {
            reference      = pixels;
            referenceDraws = draws;
            EXPECT_GT(reference[48 * 96 + 48] & 0x00ffffffu, 0u);
        }
        else if (scenario == 1)
        {
            EXPECT_TRUE(std::equal(pixels.begin(), pixels.end(), reference.begin(), reference.end()));
            EXPECT_EQ(draws + 3, referenceDraws);
        }
        else
        {
            EXPECT_FALSE(std::equal(pixels.begin(), pixels.end(), reference.begin(), reference.end()));
            EXPECT_EQ(draws, referenceDraws);
        }
        device->NextFrame();
    }
}

TEST_P(ConeVoxelGIIntegrationTest, ForwardDrawCacheTracksCommittedSceneAndViewChanges)
{
    device->InitializeRendererServer();
    ForwardDrawCache cache;
    ForwardDrawKey   previous;
    const Mat4       identity(1);
    for (uint32_t replacement = 0; replacement < 2; ++replacement)
    {
        SCOPED_TRACE(replacement);
        ForwardBlendSceneFixture fixture(*device);
        ASSERT_TRUE(fixture.Init(1, true));
        RenderScene& scene = *fixture.scene;
        if (replacement != 0)
        {
            // Fresh scenes have matching local revisions; identity must reject the old snapshot.
            EXPECT_NE(scene.GetIdentity(), previous.scene);
            EXPECT_EQ(scene.GetGeometryRevision(), previous.geometryRevision);
            EXPECT_EQ(scene.GetSurfaceRevision(), previous.surfaceRevision);
            EXPECT_EQ(scene.GetRenderableNodes().size(), previous.renderables);
        }
        ASSERT_TRUE(cache.Update(scene, identity));
        ASSERT_EQ(cache.draws.size(), 1u);
        EXPECT_EQ(cache.draws[0].indexCount, 12u);
        EXPECT_FALSE(cache.Update(scene, identity));
        previous = cache.key;
        if (replacement != 0)
        {
            const Mat4 translated = glm::translate(identity, Vec3(0, 0, .25f));
            EXPECT_TRUE(cache.Update(scene, translated));
            EXPECT_FALSE(cache.Update(scene, translated));
            const Mat4 rotated = glm::rotate(translated, glm::radians(180.0f), Vec3(0, 1, 0));
            EXPECT_TRUE(cache.Update(scene, rotated));
            ASSERT_EQ(cache.indices.size(), 12u);
            EXPECT_EQ(cache.indices[0], 9u);
            EXPECT_FALSE(cache.Update(scene, rotated));
            ASSERT_TRUE(cache.Update(scene, identity));
            EXPECT_EQ(cache.indices[0], 0u);

            ASSERT_TRUE(scene.SetInstanceTransform(1, glm::translate(identity, Vec3(.5f, 0, 0))));
            ASSERT_TRUE(scene.CommitGeometryUpdates());
            ASSERT_TRUE(cache.Update(scene, identity));
            EXPECT_EQ(cache.draws.size(), 4u);
            EXPECT_FALSE(cache.Update(scene, identity));

            asset::Vertex vertex = scene.GetVertices()[0];
            vertex.pos.z         = 2.0f;
            ASSERT_TRUE(scene.UpdateVertices(0, MakeVecView(static_cast<const asset::Vertex*>(&vertex), 1)));
            ASSERT_TRUE(scene.CommitGeometryUpdates());
            ASSERT_TRUE(cache.Update(scene, identity));
            EXPECT_EQ(cache.indices[0], 3u);
            EXPECT_FALSE(cache.Update(scene, identity));

            ASSERT_TRUE(scene.SetInstanceEnabled(1, false));
            ASSERT_TRUE(scene.CommitGeometryUpdates());
            ASSERT_TRUE(cache.Update(scene, identity));
            ASSERT_EQ(cache.draws.size(), 1u);
            EXPECT_EQ(cache.draws[0].indexCount, 6u);
            EXPECT_FALSE(cache.Update(scene, identity));
            ASSERT_TRUE(scene.SetInstanceEnabled(1, true));
            ASSERT_TRUE(scene.CommitGeometryUpdates());
            ASSERT_TRUE(cache.Update(scene, identity));
            EXPECT_EQ(cache.indices.size(), 12u);

            sg::MaterialData material    = scene.GetMaterialsData()[0];
            material.surfaceProperties.y = static_cast<float>(sg::AlphaMode::Opaque);
            ASSERT_TRUE(scene.UpdateMaterial(0, material));
            ASSERT_TRUE(scene.CommitGeometryUpdates());
            ASSERT_TRUE(cache.Update(scene, identity));
            EXPECT_TRUE(cache.draws.empty());
            EXPECT_EQ(cache.opaque[static_cast<uint32_t>(sg::MeshTopology::Triangles)].size(), 2u);
            EXPECT_FALSE(cache.hasBlend);
            EXPECT_FALSE(cache.Update(scene, identity));

            material.sheenColorTransmission.w = .5f;
            ASSERT_TRUE(scene.UpdateMaterial(0, material));
            ASSERT_TRUE(scene.CommitGeometryUpdates());
            ASSERT_TRUE(cache.Update(scene, identity));
            EXPECT_EQ(cache.draws.size(), 2u);
            EXPECT_TRUE(cache.opaque[static_cast<uint32_t>(sg::MeshTopology::Triangles)].empty());
            EXPECT_FALSE(cache.hasBlend);
            EXPECT_FALSE(cache.Update(scene, identity));

            const uint64_t geometryRevision = scene.GetGeometryRevision();
            material.baseColorFactor.x      = .5f;
            ASSERT_TRUE(scene.UpdateMaterial(0, material));
            ASSERT_TRUE(scene.CommitGeometryUpdates());
            EXPECT_EQ(scene.GetGeometryRevision(), geometryRevision);
            EXPECT_TRUE(cache.Update(scene, identity));
            EXPECT_FALSE(cache.Update(scene, identity));

            ASSERT_TRUE(scene.SetMaterialVariant(0));
            ASSERT_TRUE(scene.CommitGeometryUpdates());
            ASSERT_TRUE(cache.Update(scene, identity));
            EXPECT_TRUE(cache.draws.empty());
            const HeapVector<SceneMeshDraw>& opaque = cache.opaque[static_cast<uint32_t>(sg::MeshTopology::Triangles)];
            ASSERT_EQ(opaque.size(), 2u);
            EXPECT_EQ(opaque[0].materialIndex, 1u);
            EXPECT_EQ(opaque[1].materialIndex, 1u);
            EXPECT_FALSE(cache.Update(scene, identity));
        }
    }
}
