# ZenTest

Engine, renderer, editor, UI and Python regression tests for
[ZenEngine](https://github.com/proton991/ZenEngine). Third-party dependency tests
remain owned by their respective repositories.

The engine includes this repository when `BUILD_TESTING=ON`. Initialize submodules
with `git submodule update --init --recursive` before configuring ZenEngine.

```sh
cmake --preset x64-windows-msvc-debug
cmake --build --preset x64-windows-msvc-debug
ctest --test-dir build/x64-windows-msvc-debug -L unit --output-on-failure
```

The commands above run from the engine checkout. To configure ZenTest directly
in a compiler developer shell:

```sh
cmake -S . -B build -G Ninja -DZEN_ENGINE_SOURCE_DIR=/path/to/ZenEngine -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build -L unit --output-on-failure
```

Native and Vulkan integration suites require a display, compatible GPU and Vulkan
validation layers. `SDL3WindowProbe` remains a separately configured Windows probe;
see its README. `SceneRegression` retains the legacy viewer solely as a test fixture
and capture driver (`scene_renderer_demo` preserves existing automation commands).
Python test modules run through CTest with the engine tools on `PYTHONPATH`.
The coverage-arithmetic script is also runnable directly.

Submodule checkouts start on a detached HEAD, so run `git switch main` before committing.
Push changes here separately; ZenEngine uses the latest `main` from its next configure.
