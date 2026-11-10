#include "../../ZenApp/CameraPath.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace
{
using namespace zen;
using namespace zen::runtime;

TEST(CameraPath, HoldsEndpointsAndInterpolatesBySceneFrame)
{
    const HeapVector<CameraPathKey> keys{{4, Vec3(0, 0, 2), Vec3(0)}, {12, Vec3(4, 0, 2), Vec3(4, 0, 0)}};
    NavigationCameraState           base;
    base.aspect = 2;
    EXPECT_EQ(CameraPathState(keys, 1, base).eye, keys.front().eye);
    EXPECT_EQ(CameraPathState(keys, 4, base).eye, keys.front().eye);
    EXPECT_EQ(CameraPathState(keys, 8, base).eye, Vec3(2, 0, 2));
    EXPECT_EQ(CameraPathState(keys, 8, base).target, Vec3(2, 0, 0));
    EXPECT_EQ(CameraPathState(keys, 20, base).eye, keys.back().eye);
    EXPECT_EQ(CameraPathState(keys, 8, base).aspect, base.aspect);
}

TEST(CameraPath, RejectsInvalidFilesWithoutPublishingPartialKeys)
{
    const std::filesystem::path path        = std::filesystem::temp_directory_path() / "zen-camera-path-test.json";
    const char*                 documents[] = {
        R"({"keys":[{"frame":1,"eye":[0,0,2],"target":[0,0,0]},{"frame":2,"eye":[1,0,2],"target":[0,0,0]}]})",
        R"({"keys":[{"frame":1,"eye":[0,0,2],"target":[0,0,0]},{"frame":1,"eye":[1,0,2],"target":[0,0,0]}]})",
        R"({"keys":[{"frame":0,"eye":[0,0,2],"target":[0,0,0]},{"frame":2,"eye":[1,0,2],"target":[0,0,0]}]})",
        R"({"keys":[{"frame":1,"eye":[0,0,2],"target":[0,0,0]},{"frame":2,"eye":[1e100,0,2],"target":[0,0,0]}]})",
        R"({"keys":[{"frame":1,"eye":[0,0,2],"target":[0,0,0]}]})",
        R"({"keys":)"};
    for (size_t index = 0; index < std::size(documents); ++index)
    {
        SCOPED_TRACE(index);
        std::ofstream file(path);
        file << documents[index];
        file.close();
        HeapVector<CameraPathKey> keys{{99, Vec3(1), Vec3(0)}};
        std::string               error;
        EXPECT_EQ(ReadCameraPath(path.string(), keys, error), index == 0);
        EXPECT_EQ(keys.size(), index == 0 ? 2u : 0u);
        EXPECT_EQ(error.empty(), index == 0);
    }
    std::error_code fileError;
    std::filesystem::remove(path, fileError);
    EXPECT_FALSE(fileError);
}
} // namespace
