class ReSTIRRayQueryTest : public RayQueryIntegrationTest
{
protected:
    void CheckLightOccluders(SceneRayQuery& queries, RenderScene& scene)
    {
        ShaderProgramManager::GetInstance().StoreProgram(
            ZEN_NEW() ComputeFileSP(device, "LightOccludersSP", "ShadowMapping/light_occluders.comp.spv"));
        ShaderProgramManager::GetInstance().StoreProgram(
            ZEN_NEW() ComputeFileSP(device, "LightOccluderCheckSP", "VoxelGI/Calibration/light_occluder_check.comp.spv"));
        SceneUniformData lighting;
        lighting.lightInfo.x               = 1;
        lighting.lights[0].directionType.w = 1;
        lighting.lights[0].colorIntensity  = Vec4(1);
        lighting.lights[0].coneShadow.z    = 1;
        VoxelGIUniformData gi;
        gi.gridMinVoxelSize.w = 1;
        for (uint32_t frame = 0; frame < 3; ++frame)
        {
            SCOPED_TRACE(frame);
            lighting.lights[0].positionRange = Vec4(frame == 1 ? 0.7f : 0.0f, 0, 0, 0);
            if (frame == 2)
            {
                Mat4 transform(1);
                transform[3].z = 0.5f;
                ASSERT_TRUE(scene.SetInstanceTransform(0, transform));
                ASSERT_TRUE(scene.CommitGeometryUpdates());
                ASSERT_TRUE(scene.Update());
            }
            RenderGraph& graph = *device->GetCurrentFrameRDG();
            ASSERT_TRUE(graph.Begin());
            ASSERT_TRUE(queries.BuildRenderGraph(scene));
            RHIBuffer* cache = Buffer(MaxSceneLights * 6u * 16u * 16u * 3u * sizeof(Vec4),
                                      BitField<RHIBufferUsageFlagBits>(RHIBufferUsageFlagBits::eStorageBuffer), nullptr,
                                      RHIBufferAllocateType::eGPU);
            BitField<RHIBufferUsageFlagBits> outputUsage;
            outputUsage.SetFlags(RHIBufferUsageFlagBits::eStorageBuffer, RHIBufferUsageFlagBits::eTransferSrcBuffer);
            RHIBuffer* output = Buffer(4096 * sizeof(glm::uvec4), outputUsage, nullptr, RHIBufferAllocateType::eGPU);
            RHIBuffer* readback =
                Buffer(4096 * sizeof(glm::uvec4), BitField<RHIBufferUsageFlagBits>(RHIBufferUsageFlagBits::eTransferDstBuffer),
                       nullptr, RHIBufferAllocateType::eCPURead);
            RDGComputePassDesc build;
            build.SetShaderProgramName("LightOccludersSP");
            build.SetPassTag("BuildTestOccluders");
            queries.BindInputs(build, scene);
            build.BindValue("uGISettings", gi);
            build.BindValue("uSceneData", lighting);
            build.BindStorageBuffer("LightOccluders", cache, RDGContentGuarantee::eFullWrite);
            graph.AddComputePass(std::move(build)).RecordPassCommands([](RDGPassCmdEncoder& encoder) {
                encoder.Dispatch(2, 2, MaxSceneLights * 6);
            });
            RDGComputePassDesc check;
            check.SetShaderProgramName("LightOccluderCheckSP");
            check.SetPassTag("CheckTestOccluders");
            queries.BindInputs(check, scene);
            check.BindValue("uGISettings", gi);
            check.BindValue("uSceneData", lighting);
            check.BindStorageBuffer("LightOccluders", cache);
            check.BindStorageBuffer("Results", output, RDGContentGuarantee::eFullWrite);
            graph.AddComputePass(std::move(check)).RecordPassCommands([](RDGPassCmdEncoder& encoder) {
                encoder.Dispatch(64, 1, 1);
            });
            graph.AddTransferPass("ReadTestOccluders")
                .CopyBuffer(output, readback, {0, 0, 4096 * sizeof(glm::uvec4)})
                .NeverCull();
            ASSERT_TRUE(graph.End()) << graph.GetResult().message;
            ASSERT_TRUE(device->ExecuteRenderGraph(graph)) << graph.GetResult().message;
            queries.OnRenderGraphExecuted(RenderFrameResult::eSubmitted);
            device->FlushRHIThread();
            device->WaitForIdle();
            const glm::uvec4* result = reinterpret_cast<const glm::uvec4*>(readback->Map());
            ASSERT_NE(result, nullptr);
            uint32_t cached = 0;
            for (uint32_t index = 0; index < 4096; ++index)
            {
                EXPECT_EQ(result[index].z, 0u) << "segment " << index;
                cached += result[index].x;
            }
            EXPECT_GT(cached, 50u);
            readback->Unmap();
            device->NextFrame();
        }
    }

    RHITexture* Texture(RenderGraph& graph, DataFormat format, const void* data, uint32_t bytes)
    {
        RHITextureCreateInfo info;
        info.type   = RHITextureType::e2D;
        info.width  = 8;
        info.height = info.depth = 1;
        info.format              = format;
        info.usageFlags.SetFlags(RHITextureUsageFlagBits::eSampled, RHITextureUsageFlagBits::eStorage,
                                 RHITextureUsageFlagBits::eTransferDst);
        RHITexture* texture = device->CreateTexture(info);
        textures.push_back(texture);
        if (data != nullptr)
        {
            BitField<RHIBufferUsageFlagBits> usage;
            usage.SetFlag(RHIBufferUsageFlagBits::eTransferSrcBuffer);
            RHIBuffer* upload = Buffer(bytes, usage, data);
            graph.GetResourceManager()->ImportHostWrittenBuffer(upload);
            RHIBufferTextureCopyRegion region;
            region.textureSize = Vec3i(8, 1, 1);
            region.textureSubresources.aspect.SetFlag(RHITextureAspectFlagBits::eColor);
            graph.AddTransferPass("UploadReSTIRReceiver").CopyBufferToTexture(upload, texture, region);
        }
        else
        {
            graph.AddTransferPass("ClearReSTIRObservation").ClearTexture(texture, Color(0));
        }
        return texture;
    }
};

TEST_P(ReSTIRRayQueryTest, TemporalAndCheckerboardReuseRejectForeignSurfacesResetAndOcclusion)
{
    if (device->GetGPUInfo().rayQuery.IsUsable())
    {
        ShaderProgramManager::GetInstance().StoreProgram(
            ZEN_NEW() ComputeFileSP(device, "ReSTIRTemporalSP", "VoxelGI/restir_temporal.comp.spv"));
        ShaderProgramManager::GetInstance().StoreProgram(
            ZEN_NEW() ComputeFileSP(device, "ReSTIRSpatialSP", "VoxelGI/restir_spatial.comp.spv"));
        sg::Scene source;
        source.GetAABB()                 = sg::AABB(Vec3(-2), Vec3(2));
        UniquePtr<sg::Material> material = MakeUnique<sg::Material>("opaque");
        material->index                  = 0;
        material->data.baseColorFactor   = Vec4(1);
        UniquePtr<sg::SubMesh> primitive = MakeUnique<sg::SubMesh>("visibility occluder", 0, 3, 3);
        primitive->SetMaterial(0, material.Get());
        UniquePtr<sg::Mesh> mesh = MakeUnique<sg::Mesh>("visibility occluder");
        mesh->SetAABB(Vec3(-1, -1, 0.5f), Vec3(1, 1, 0.5f));
        mesh->AddSubMesh(primitive.Get());
        HeapVector<UniquePtr<sg::Node>> nodes;
        UniquePtr<sg::Node>             node = MakeUnique<sg::Node>(0, "visibility occluder");
        node->SetData(0, Mat4(1));
        node->AddComponent(mesh.Get());
        source.AddRenderableNode(node.Get());
        nodes.push_back(std::move(node));
        source.SetNodes(std::move(nodes));
        source.AddComponent(std::move(primitive));
        source.AddComponent(std::move(mesh));
        source.AddComponent(std::move(material));
        asset::Vertex vertices[3]{};
        for (uint32_t i = 0; i < 3; ++i)
        {
            vertices[i].pos    = Vec4(i == 0 ? -1 : i == 1 ? 1 : 0, i == 2 ? 1 : -1, 0.5f, 1);
            vertices[i].normal = Vec4(0, 0, 1, 0);
            vertices[i].color  = Vec4(1);
        }
        uint32_t   indices[] = {0, 1, 2};
        sg::Camera camera(Vec3(0, 0, 3), Vec3(0), 1);
        SceneData  data{};
        data.pScene      = &source;
        data.pVertices   = vertices;
        data.numVertices = 3;
        data.pIndices    = indices;
        data.numIndices  = 3;
        data.pCamera     = &camera;
        RenderScene scene(device, data);
        scene.LoadSceneMaterials();
        scene.PrepareBuffers();
        ASSERT_TRUE(scene.Update());
        SceneRayQuery      queries(device);
        RHISampler*        sampler   = device->CreateSampler({});
        constexpr float    pi        = 3.14159265358979323846f;
        constexpr uint32_t packedOne = 256u | (256u << 9u) | (256u << 18u) | (16u << 27u);
        for (uint32_t frame = 0; frame < 6; ++frame)
        {
            SCOPED_TRACE(frame);
            const bool   reflection = frame == 5;
            RenderGraph& graph      = *device->GetCurrentFrameRDG();
            ASSERT_TRUE(graph.Begin());
            ASSERT_TRUE(queries.BuildRenderGraph(scene));
            HeapVector<float>      depth(8, 0.5f), integral(8, 1.0f);
            HeapVector<Vec4>       positions(8, Vec4(0, 0, 0, 1)), oldPositions   = positions;
            HeapVector<Vec4>       normals(8, Vec4(0.5f, 0.5f, 0, 0)), oldNormals = normals;
            HeapVector<Vec4>       roughness(8, Vec4(0, 0.5f, 0, 0));
            HeapVector<Vec2>       motion(8, Vec2(0));
            HeapVector<Vec4>       lengths(8, Vec4(0, 0, 0, 0.5f));
            HeapVector<glm::uvec2> receivers(8, glm::uvec2(0x80008000u, 1)), oldReceivers = receivers;
            receivers[1].y    = 2;
            oldPositions[2].z = 0.1f;
            oldNormals[3]     = Vec4(0);
            positions[4].w    = 0;
            depth[5]          = 1;
            receivers[6].y    = 0;
            // At normal incidence and roughness 0.5, this unit-integral fixture's
            // GGX density is 4/pi. Normalize the input estimates to Lo in both cases.
            const float                   density = reflection ? 4.0f / pi : 1.0f / pi;
            const PackedGIReservoir       fresh{0, 0, 1, 0x81ffffffu, packedOne, 1.0f / density};
            const PackedGIReservoir       previous{0, 0, 1, 0xe4ffffffu, packedOne + (1u << 27u), 1.0f / density};
            HeapVector<PackedGIReservoir> current(8, fresh), history(8, previous), empty(8, PackedGIReservoir{});
            if (frame == 3)
            {
                // Four identical neighbors and one center: uniform pairwise MIS gives
                // the center 1/5 of the estimate, rather than 1/2.
                current[4].radiance += 1u << 27u;
            }
            if (frame == 4)
            {
                oldPositions[7].x = 0.1f;
                current[7].weight = 0.0f;
            }
            BitField<RHIBufferUsageFlagBits> usage;
            usage.SetFlags(RHIBufferUsageFlagBits::eStorageBuffer, RHIBufferUsageFlagBits::eTransferSrcBuffer);
            RHIBuffer* initial      = Buffer(8 * sizeof(fresh), usage, current.data());
            RHIBuffer* old          = Buffer(8 * sizeof(fresh), usage, history.data());
            RHIBuffer* noReflection = Buffer(8 * sizeof(fresh), usage, empty.data());
            RHIBuffer* output = frame == 3 ? initial : Buffer(8 * sizeof(fresh), usage, nullptr, RHIBufferAllocateType::eGPU);
            RHIBuffer* specularOutput = Buffer(8 * sizeof(fresh), usage, nullptr, RHIBufferAllocateType::eGPU);
            RHIBuffer* readback =
                Buffer(8 * sizeof(fresh), BitField<RHIBufferUsageFlagBits>(RHIBufferUsageFlagBits::eTransferDstBuffer), nullptr,
                       RHIBufferAllocateType::eCPURead);
            graph.GetResourceManager()->ImportHostWrittenBuffer(initial);
            graph.GetResourceManager()->ImportHostWrittenBuffer(old);
            graph.GetResourceManager()->ImportHostWrittenBuffer(noReflection);
            RHITexture* inputs[] = {Texture(graph, DataFormat::eR32SFloat, depth.data(), 8 * sizeof(float)),
                                    Texture(graph, DataFormat::eR32G32B32A32SFloat, normals.data(), 8 * sizeof(Vec4)),
                                    Texture(graph, DataFormat::eR32G32B32A32SFloat, roughness.data(), 8 * sizeof(Vec4)),
                                    Texture(graph, DataFormat::eR32G32UInt, receivers.data(), 8 * sizeof(glm::uvec2)),
                                    Texture(graph, DataFormat::eR32G32SFloat, motion.data(), 8 * sizeof(Vec2)),
                                    Texture(graph, DataFormat::eR32G32B32A32SFloat, positions.data(), 8 * sizeof(Vec4)),
                                    Texture(graph, DataFormat::eR32G32B32A32SFloat, oldPositions.data(), 8 * sizeof(Vec4)),
                                    Texture(graph, DataFormat::eR32G32B32A32SFloat, oldNormals.data(), 8 * sizeof(Vec4)),
                                    Texture(graph, DataFormat::eR32G32UInt, oldReceivers.data(), 8 * sizeof(glm::uvec2)),
                                    Texture(graph, DataFormat::eR32G32B32A32SFloat, lengths.data(), 8 * sizeof(Vec4)),
                                    Texture(graph, DataFormat::eR32SFloat, integral.data(), 8 * sizeof(float))};
            RHITexture* bounce   = Texture(graph, DataFormat::eR16G16B16A16SFloat, nullptr, 0);
            RHITexture* specular = Texture(graph, DataFormat::eR16G16B16A16SFloat, nullptr, 0);
            const char* names[]  = {"receiverDepth",    "receiverNormal",   "receiverRoughness", "receiverSurface",
                                    "receiverMotion",   "receiverPosition", "previousPosition",  "previousNormal",
                                    "previousReceiver", "previousLength",   "restirLobeIntegral"};
            HybridGIUniformData hybrid;
            hybrid.sampling             = glm::uvec4(frame, 1, 32, 1);
            hybrid.bounce               = glm::uvec4(1, frame == 1 || frame == 4 ? 1 : 0, 0, 0);
            hybrid.reflection           = glm::uvec4(reflection ? 1 : 0, reflection ? 1 : 0, 0, 0);
            hybrid.rejection            = Vec4(0.001f, 1, 1, 0);
            hybrid.reservoir            = glm::uvec4(reflection ? 2 : 1, 20, 7, 0);
            hybrid.previousViewPosition = Vec4(0, 0, 3, 1);
            VoxelGIUniformData gi;
            for (uint32_t stage = 0; stage < (frame == 3 ? 2u : 1u); ++stage)
            {
                RDGComputePassDesc pass;
                pass.SetShaderProgramName(frame == 3 ? "ReSTIRSpatialSP" : "ReSTIRTemporalSP");
                pass.SetQueuePreference(RDGQueuePreference::ePreferAsyncCompute);
                pass.BindValue("uHybridData", hybrid);
                pass.BindValue("uGISettings", gi);
                pass.BindValue("uSceneData", scene.GetSceneUniformData(), sizeof(SceneUniformData));
                pass.BindValue("uReSTIRData",
                               glm::uvec4(frame == 3 ? 1 : 0, reflection ? 1 : 0, reflection ? 0 : 1, stage + 1));
                queries.BindInputs(pass, scene);
                for (uint32_t i = 0; i < std::size(inputs); ++i)
                {
                    pass.BindSampledTexture(names[i], sampler, inputs[i]->GetDefaultView());
                }
                pass.BindStorageBuffer("GIReservoirCurrent", initial);
                pass.BindStorageBuffer("GIReservoirPrevious", old);
                pass.BindStorageBuffer("GIReservoirOutput", output);
                pass.BindStorageBuffer("ReflectionReservoirCurrent", reflection ? initial : noReflection);
                pass.BindStorageBuffer("ReflectionReservoirPrevious", reflection ? old : noReflection);
                pass.BindStorageBuffer("ReflectionReservoirOutput", specularOutput, RDGContentGuarantee::eFullWrite);
                pass.BindStorageImage("rawBounce", bounce->GetDefaultView());
                pass.BindStorageImage("rawSpecular", specular->GetDefaultView());
                graph.AddComputePass(std::move(pass)).RecordPassCommands([](RDGPassCmdEncoder& encoder) {
                    encoder.Dispatch(1, 1, 1);
                });
            }
            graph.AddTransferPass("ReadReSTIRTemporal")
                .CopyBuffer(reflection ? specularOutput : output, readback, {0, 0, 8 * sizeof(fresh)})
                .NeverCull();
            ASSERT_TRUE(graph.End()) << graph.GetResult().message;
            ASSERT_TRUE(device->ExecuteRenderGraph(graph)) << graph.GetResult().message;
            queries.OnRenderGraphExecuted(RenderFrameResult::eSubmitted);
            device->FlushRHIThread();
            device->WaitForIdle();
            const PackedGIReservoir* result = reinterpret_cast<const PackedGIReservoir*>(readback->Map());
            ASSERT_NE(result, nullptr);
            for (uint32_t i = 0; i < 8; ++i)
            {
                SCOPED_TRACE(i);
                const bool     valid        = i != 5 && i != 6;
                const bool     reused       = (frame == 1 || frame == 4 || reflection) && (i == 0 || i == 7);
                const uint32_t historyCount = reflection ? 7u : 20u;
                const uint32_t count        = frame == 3 && (i == 2 || i == 3) ? 5u : reused ? historyCount + 1u : 1u;
                EXPECT_EQ((result[i].normalCount >> 24u) & 127u, valid ? count : 0u);
                const float radiance = float(result[i].radiance & 511u) * std::exp2(float(int(result[i].radiance >> 27u) - 24));
                const float expected = !valid || (frame == 4 && i == 7) ? 0.0f
                                     : frame == 3 && i == 3             ? 1.8f
                                     : frame == 3 && i == 4             ? 2.0f
                                     : reused ? float(2u * historyCount + 1u) / float(historyCount + 1u)
                                              : 1.0f;
                EXPECT_NEAR(radiance * result[i].weight * density, expected, 1e-4f);
            }
            readback->Unmap();
            device->NextFrame();
        }
        CheckLightOccluders(queries, scene);
        queries.Destroy();
        scene.Destroy();
    }
    else
    {
        GTEST_SKIP() << "Ray queries unavailable on this device";
    }
}
INSTANTIATE_TEST_SUITE_P(SubmissionModes, ReSTIRRayQueryTest, testing::Values(0u, 1u, 2u, 3u));
