#include "ImGui/LightingBackendControls.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>

namespace zen::ui
{
namespace
{
using Provider = rc::VoxelGISettings::RayProvider;

} // namespace

TEST(GITracingTest, MethodSwitchKeepsLightingAndBackendSelection)
{
    rc::VoxelGISettings settings;
    settings.rayProvider       = Provider::Voxel;
    settings.indirectDiffuse   = false;
    settings.specularOcclusion = false;
    settings.rtShadows         = false;
    for (uint32_t index = 0; index < static_cast<uint32_t>(rc::GIMethod::Count); ++index)
    {
        const rc::GIMethod method = static_cast<rc::GIMethod>(index);
        rc::SetGIMethod(settings, method);
        EXPECT_EQ(rc::GetGIMethod(settings), method);
        EXPECT_EQ(settings.rayProvider, Provider::Voxel);
        EXPECT_FALSE(settings.indirectDiffuse);
        EXPECT_FALSE(settings.specularOcclusion);
        EXPECT_FALSE(settings.rtShadows);
    }
}

TEST(GITracingTest, EveryChoiceHasALabel)
{
    for (uint32_t index = 0; index < static_cast<uint32_t>(rc::GIMethod::Count); ++index)
    {
        EXPECT_GT(std::string(GetGIMethodLabel(static_cast<rc::GIMethod>(index))).size(), 0u);
    }
    EXPECT_STREQ(GetGITracingLabel(rc::GITracing::RayTracingPipeline), "Hardware RT pipeline");
    EXPECT_STREQ(GetGIDenoiserLabel(rc::GIDenoiser::Temporal), "Temporal only");
    EXPECT_STREQ(GetGIShadowsLabel(rc::GIShadows::RayTracedReuse), "Ray traced with reuse");
}

// The GI settings panel shown by the editor and the runtime UI. Drawn headlessly; ImGui logs the drawn text.
class LightingBackendControlsTest : public testing::Test
{
protected:
    void SetUp() override
    {
        context               = ImGui::CreateContext();
        ImGuiIO& io           = ImGui::GetIO();
        io.IniFilename        = nullptr;
        io.DisplaySize        = ImVec2(900, 1400);
        io.DeltaTime          = 1.0f / 60.0f;
        unsigned char* pixels = nullptr;
        int            width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }

    void TearDown() override
    {
        ImGui::DestroyContext(context);
    }

    std::string Draw(rc::VoxelGIRuntimeSettings&       settings,
                     bool                              hardwareReady,
                     const char*                       reason    = "ray queries unsupported",
                     const rc::VoxelGIRuntimeSettings* effective = nullptr)
    {
        GIControlsContext controls;
        controls.effective          = effective;
        controls.hardwareReady      = hardwareReady;
        controls.reason             = reason;
        controls.rayQueries         = true;
        controls.rayTracingPipeline = true;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(900, 1400));
        ImGui::Begin("GI tracing test");
        ImGui::LogToBuffer();
        EXPECT_FALSE(DrawGIControls(settings, controls));
        const std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
        ImGui::End();
        ImGui::Render();
        std::printf("%s\n", text.c_str());
        return text;
    }

    std::string Draw(rc::VoxelGISettings& cone, bool hardwareReady, const char* reason = "ray queries unsupported")
    {
        rc::VoxelGIRuntimeSettings settings;
        settings.cone          = cone;
        const std::string text = Draw(settings, hardwareReady, reason);
        cone                   = settings.cone;
        return text;
    }

    ImGuiContext* context{nullptr};
};

TEST_F(LightingBackendControlsTest, ComputeShowsOneTierAndIndependentLightingEnables)
{
    rc::VoxelGISettings settings;
    settings.rayProvider   = Provider::Voxel;
    const std::string text = Draw(settings, false);
    EXPECT_NE(text.find("Tracing"), std::string::npos);
    EXPECT_NE(text.find("Compute (distance field)"), std::string::npos);
    EXPECT_NE(text.find("Diffuse GI"), std::string::npos);
    EXPECT_NE(text.find("Specular GI"), std::string::npos);
    EXPECT_NE(text.find("Direct shadows"), std::string::npos);
    EXPECT_NE(text.find("Distance field cells per axis"), std::string::npos);
    EXPECT_NE(text.find("Material voxels per axis"), std::string::npos);
    EXPECT_NE(text.find("GI resolution"), std::string::npos);
    EXPECT_NE(text.find("Compute uses shadow maps"), std::string::npos);
    EXPECT_EQ(text.find("Hardware RT is"), std::string::npos);
    EXPECT_EQ(text.find("Reuse neighbors"), std::string::npos);
}

// Compute GI preparation is reported above the scene view, not in the panel.
TEST_F(LightingBackendControlsTest, ComputePreparationIsLeftToTheSceneView)
{
    rc::VoxelGISettings settings;
    settings.rayProvider = Provider::Voxel;
    std::string text     = Draw(settings, true, "field_preparing");
    EXPECT_EQ(text.find("Preparing Compute GI"), std::string::npos);
    EXPECT_EQ(text.find("field_preparing"), std::string::npos);
}

TEST_F(LightingBackendControlsTest, SceneStatusDistinguishesPreparationRefinementAndReadiness)
{
    EXPECT_EQ(GetGILightingStatus(GILightingState::Inactive), nullptr);
    EXPECT_STREQ(GetGILightingStatus(GILightingState::Preparing), "Preparing GI...");
    EXPECT_STREQ(GetGILightingStatus(GILightingState::Refining), "GI active. Refining lighting...");
    EXPECT_STREQ(GetGILightingStatus(GILightingState::Ready), "GI ready");
    EXPECT_STREQ(GetGILightingStatus(GILightingState::Unavailable), "GI unavailable. Check Rendering for details.");
}

TEST_F(LightingBackendControlsTest, HardwareReportsWhetherItIsActive)
{
    rc::VoxelGISettings settings;
    rc::SetGITracing(settings, rc::GITracing::RayTracingPipeline);
    std::string text = Draw(settings, true);
    EXPECT_NE(text.find("Hardware RT is active"), std::string::npos);
    EXPECT_NE(text.find("Shadow method"), std::string::npos);
    EXPECT_NE(text.find("Ray traced"), std::string::npos);
    EXPECT_EQ(text.find("Distance field cells per axis"), std::string::npos);
    EXPECT_EQ(text.find("Compute uses shadow maps"), std::string::npos);
    text = Draw(settings, false);
    EXPECT_NE(text.find("ray queries unsupported"), std::string::npos);

    EXPECT_TRUE(settings.rtShadows);
    EXPECT_EQ(settings.rayProvider, Provider::Hardware);
}

TEST_F(LightingBackendControlsTest, ScreenProbesOnlyShowTheirActiveControls)
{
    rc::VoxelGISettings settings;
    rc::SetGITracing(settings, rc::GITracing::RayTracingPipeline);
    rc::SetGIMethod(settings, rc::GIMethod::PixelReSTIR);
    std::string text = Draw(settings, true);
    EXPECT_NE(text.find("Reuse neighbors"), std::string::npos);
    EXPECT_NE(text.find("Pixel denoiser"), std::string::npos);
    rc::SetGIMethod(settings, rc::GIMethod::ScreenProbes);
    text = Draw(settings, true);
    EXPECT_NE(text.find("Full-resolution probe gather"), std::string::npos);
    EXPECT_NE(text.find("Probe filter"), std::string::npos);
    EXPECT_EQ(text.find("GI resolution"), std::string::npos);
    EXPECT_EQ(text.find("Spatial reuse"), std::string::npos);
    EXPECT_EQ(text.find("Reuse neighbors"), std::string::npos);
}

// A method the active tier cannot run says what renders instead.
TEST_F(LightingBackendControlsTest, UnavailableMethodsNameTheirRequirement)
{
    rc::VoxelGISettings settings;
    rc::SetGITracing(settings, rc::GITracing::RayQueries);
    rc::SetGIMethod(settings, rc::GIMethod::ScreenProbes);
    std::string text = Draw(settings, true);
    EXPECT_NE(text.find("The world cache needs the RT pipeline or Compute"), std::string::npos);
    settings.rayProvider = Provider::Voxel;
    rc::SetGIMethod(settings, rc::GIMethod::PixelReSTIR);
    text = Draw(settings, false);
    EXPECT_NE(text.find("ReSTIR reuse needs hardware RT"), std::string::npos);
}

TEST_F(LightingBackendControlsTest, DenoiserListsItsModeAndWarnsWhenReduced)
{
    rc::VoxelGISettings settings;
    rc::SetGITracing(settings, rc::GITracing::RayTracingPipeline);
    std::string text = Draw(settings, true);
    EXPECT_NE(text.find("Spatiotemporal"), std::string::npos);
    EXPECT_NE(text.find("Denoiser details"), std::string::npos);
    EXPECT_EQ(text.find("Noisier"), std::string::npos);
    rc::SetGIDenoiser(settings, rc::GIDenoiser::Off);
    text = Draw(settings, true);
    EXPECT_NE(text.find("Off"), std::string::npos);
    EXPECT_NE(text.find("Noisier"), std::string::npos);
    EXPECT_EQ(text.find("Denoiser details"), std::string::npos);
}

// The optimal configuration shows what it resolved to on this device, and the matrix of combinations.
TEST_F(LightingBackendControlsTest, OptimalShowsTheResolvedChoice)
{
    rc::VoxelGIRuntimeSettings settings;
    ASSERT_TRUE(rc::ApplyVoxelGIPreset(rc::VoxelGIPreset::Optimal, settings));
    RHIGPUInfo gpu;
    gpu.rayQuery                         = {true, true, true, true, true, 256};
    gpu.rayTracing                       = {true, false, RHIShaderInvocationReorder::eNone, 32, 32, 64, 4096, 2, 1048576, 32};
    rc::VoxelGIRuntimeSettings effective = settings;
    rc::ResolveVoxelGIAutomaticProvider(effective, gpu);
    const std::string text = Draw(settings, true, "", &effective);
    EXPECT_NE(text.find("Chosen for this GPU at startup: Screen probes + world cache / Hardware RT pipeline"),
              std::string::npos);
    EXPECT_NE(text.find("All combinations"), std::string::npos);
    EXPECT_NE(text.find("RT pipeline"), std::string::npos);
    EXPECT_NE(text.find("Available"), std::string::npos);
    EXPECT_TRUE(settings.optimal);
    EXPECT_EQ(settings.cone.rayProvider, Provider::Auto);
}

TEST_F(LightingBackendControlsTest, OffPassesSayWhatReplacesThem)
{
    rc::VoxelGISettings settings;
    settings.rayProvider = Provider::Voxel;
    std::string text     = Draw(settings, false);
    EXPECT_EQ(text.find("unoccluded environment"), std::string::npos);
    settings.indirectDiffuse = false;
    text                     = Draw(settings, false);
    EXPECT_NE(text.find("unoccluded environment"), std::string::npos);
}

} // namespace zen::ui
