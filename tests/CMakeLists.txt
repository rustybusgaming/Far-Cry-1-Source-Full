################################################################################
# Tests for the web port's own code.
#
# Scope is deliberately narrow: this is not a test suite for CryEngine. It
# covers the code THIS PORT introduces -- principally WinBase.h, which is a
# reconstruction of a header that was never released and therefore has no
# original to check against.
################################################################################

add_executable(test_winbase test_winbase.cpp)
cry_apply_common_settings(test_winbase)
target_link_libraries(test_winbase PRIVATE CryCommon)

add_test(NAME winbase COMMAND test_winbase)

# The browser input backend. Its event source is separable precisely so the
# state machine can be tested here -- none of it can run in a browser under a
# unit test.
add_executable(test_webinput test_webinput.cpp
               "${PROJECT_SOURCE_DIR}/CryInput/WebInput.cpp")
cry_apply_common_settings(test_webinput)
target_include_directories(test_webinput PRIVATE "${PROJECT_SOURCE_DIR}/CryInput")
target_link_libraries(test_webinput PRIVATE CryCommon)

add_test(NAME webinput COMMAND test_webinput)

# The CryEngine-pass to WGSL translation.
#
# Built for BOTH targets, and deliberately so. It is the only part of the
# WebGPU backend that can be verified anywhere: the translation is a pure
# function, so it needs no device, no adapter and no browser. Everything else
# in that backend touches the GPU and cannot be checked without one.
add_executable(test_shadergen test_shadergen.cpp
               "${PROJECT_SOURCE_DIR}/RenderDll/XRenderWGPU/WGPUShaderGen.cpp"
               "${PROJECT_SOURCE_DIR}/RenderDll/Common/CryStateGen.cpp"
               "${PROJECT_SOURCE_DIR}/RenderDll/Common/CryPassDesc.cpp"
               "${PROJECT_SOURCE_DIR}/RenderDll/Common/CryPassGen.cpp")
cry_apply_common_settings(test_shadergen)
target_include_directories(test_shadergen PRIVATE
    "${PROJECT_SOURCE_DIR}/RenderDll/XRenderWGPU"
    "${PROJECT_SOURCE_DIR}/RenderDll/Common")
target_link_libraries(test_shadergen PRIVATE CryCommon)

add_test(NAME shadergen COMMAND test_shadergen)

# The same translation emitted as GLSL ES 3.00.
#
# This one carries more weight than its size suggests. The WebGL2 backend can
# be RUN here -- in a real browser, reading real pixels -- while the WebGPU one
# cannot, and since both emitters share their operation table, exercising this
# path is evidence about the decisions the WGSL path also depends on.
#
# It links the WGSL generator too, so one test can assert that the two
# languages agree on a description rather than each asserting its own half.
add_executable(test_glesshadergen test_glesshadergen.cpp
               "${PROJECT_SOURCE_DIR}/RenderDll/XRenderGLES/GLESShaderGen.cpp"
               "${PROJECT_SOURCE_DIR}/RenderDll/XRenderWGPU/WGPUShaderGen.cpp"
               "${PROJECT_SOURCE_DIR}/RenderDll/Common/CryStateGen.cpp"
               "${PROJECT_SOURCE_DIR}/RenderDll/Common/CryPassDesc.cpp"
               "${PROJECT_SOURCE_DIR}/RenderDll/Common/CryPassGen.cpp")
cry_apply_common_settings(test_glesshadergen)
target_include_directories(test_glesshadergen PRIVATE
    "${PROJECT_SOURCE_DIR}/RenderDll/XRenderGLES"
    "${PROJECT_SOURCE_DIR}/RenderDll/XRenderWGPU"
    "${PROJECT_SOURCE_DIR}/RenderDll/Common")
target_link_libraries(test_glesshadergen PRIVATE CryCommon)

add_test(NAME glesshadergen COMMAND test_glesshadergen)

# GS_* render state to pipeline state. Also pure, also testable without a GPU,
# and worth testing carefully: it is one integer carrying eleven things, several
# of them negative flags, and one read backwards gives a scene that renders but
# is subtly wrong.
add_executable(test_stategen test_stategen.cpp
               "${PROJECT_SOURCE_DIR}/RenderDll/Common/CryStateGen.cpp"
               "${PROJECT_SOURCE_DIR}/RenderDll/Common/CryPassDesc.cpp"
               "${PROJECT_SOURCE_DIR}/RenderDll/Common/CryPassGen.cpp")
cry_apply_common_settings(test_stategen)
target_include_directories(test_stategen PRIVATE
    "${PROJECT_SOURCE_DIR}/RenderDll/Common")
target_link_libraries(test_stategen PRIVATE CryCommon)

add_test(NAME stategen COMMAND test_stategen)

# The pipeline cache key. Pure -- WGPUPipeline.cpp keeps its key function
# outside the Emscripten guard precisely so it can be tested here -- and the
# highest-consequence of the three: a key missing a field means two passes
# silently share a pipeline and one renders with the other's shader.
add_executable(test_pipelinekey test_pipelinekey.cpp
               "${PROJECT_SOURCE_DIR}/RenderDll/XRenderWGPU/WGPUPipeline.cpp"
               "${PROJECT_SOURCE_DIR}/RenderDll/XRenderWGPU/WGPUShaderGen.cpp"
               "${PROJECT_SOURCE_DIR}/RenderDll/Common/CryStateGen.cpp"
               "${PROJECT_SOURCE_DIR}/RenderDll/Common/CryPassDesc.cpp"
               "${PROJECT_SOURCE_DIR}/RenderDll/Common/CryPassGen.cpp")
cry_apply_common_settings(test_pipelinekey)
target_include_directories(test_pipelinekey PRIVATE
    "${PROJECT_SOURCE_DIR}/RenderDll/XRenderWGPU"
    "${PROJECT_SOURCE_DIR}/RenderDll/Common")
target_link_libraries(test_pipelinekey PRIVATE CryCommon)

# Under Emscripten, WGPUPipeline.h reveals its device half and pulls in Dawn's
# headers, so the test needs the same port flag the backend does. Natively that
# half is compiled out and only the pure key function remains -- which is the
# whole reason the key lives outside the Emscripten guard.
if(EMSCRIPTEN)
    target_compile_options(test_pipelinekey PRIVATE "--use-port=emdawnwebgpu")
    target_link_options(test_pipelinekey PRIVATE "--use-port=emdawnwebgpu")
endif()

add_test(NAME pipelinekey COMMAND test_pipelinekey)

# The compiled-in module registry that replaces dlopen()/dlsym(). It links the
# whole module set, because the point of the test is that the real factories
# resolve -- a table checked against stubs would prove nothing.
add_executable(test_static_modules test_static_modules.cpp)
cry_apply_common_settings(test_static_modules)
target_include_directories(test_static_modules PRIVATE "${PROJECT_SOURCE_DIR}/CryCommon")
cry_link_module_group(test_static_modules ${CRY_HEADLESS_MODULES})
target_link_libraries(test_static_modules PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)

add_test(NAME static_modules COMMAND test_static_modules)

# The headless engine itself: does CreateSystemInterface() bring up every
# subsystem and shut down again. This is a smoke test, not a unit test -- it is
# here because it is the only thing that exercises the modules TOGETHER, and
# because the failure it guards against (a subsystem quietly dropping out of
# startup) is invisible to everything else in this directory.
add_test(NAME headless_boot COMMAND Headless)
set_tests_properties(headless_boot PROPERTIES
    TIMEOUT 120
    # Asserted rather than just "exit 0": the engine is happy to keep going
    # after a subsystem fails, so a clean exit alone would not notice.
    PASS_REGULAR_EXPRESSION "System interface created"
    FAIL_REGULAR_EXPRESSION "FAILED: CreateSystemInterface"
)

# ------------------------------------------------------------------------------
# Browser tests.
#
# These need a real browser and so are built only for wasm. They are driven by
# tests/web/run_browser_tests.py, which serves the build over HTTP -- wasm
# cannot be loaded from a file:// URL -- and drives headless Chromium through
# Playwright.
# ------------------------------------------------------------------------------
if(EMSCRIPTEN)
    # The WebGL2 context the renderer creates. Links GLESContext.cpp itself
    # rather than a copy, so this covers the code that actually ships.
    add_executable(test_glescontext
        web/test_glescontext.cpp
        "${PROJECT_SOURCE_DIR}/RenderDll/XRenderGLES/GLESContext.cpp")
    target_include_directories(test_glescontext PRIVATE
        "${PROJECT_SOURCE_DIR}/RenderDll/XRenderGLES")
    # A canvas is required, so this is an HTML target rather than bare JS.
    set_target_properties(test_glescontext PROPERTIES SUFFIX ".html")
    target_link_options(test_glescontext PRIVATE
        "SHELL:-s MIN_WEBGL_VERSION=2"
        "SHELL:-s MAX_WEBGL_VERSION=2"
        "SHELL:-s OFFSCREENCANVAS_SUPPORT=0")

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME glescontext_browser
                 COMMAND ${Python3_EXECUTABLE}
                         "${CMAKE_CURRENT_SOURCE_DIR}/web/run_browser_tests.py"
                         "$<TARGET_FILE:test_glescontext>")
        set_tests_properties(glescontext_browser PROPERTIES TIMEOUT 300)
    endif()

    # The browser host: does the engine's frame loop actually run and keep
    # yielding to the page. See run_browser_tests.py --frames for why that is
    # the thing worth asserting.
    if(Python3_Interpreter_FOUND AND TARGET CryWeb)
        add_test(NAME web_host_frames
                 COMMAND ${Python3_EXECUTABLE}
                         "${CMAKE_CURRENT_SOURCE_DIR}/web/run_browser_tests.py"
                         "$<TARGET_FILE:CryWeb>" --frames 30
                         # The quad Web/WebMain.cpp draws through
                         # IRenderer::DrawDynVB, sampled back through
                         # IRenderer::ReadFrameBuffer. The centre must be the
                         # quad's colour and the corner the clear, which
                         # together show the geometry landed in the right place
                         # and the BGRA-to-RGBA swizzle is right.
                         --expect-centre 32,128,224
                         --expect-corner 0,0,0
                         # The textured quad. Its texel is uploaded as
                         # eTF_8888 -- the engine's BGRA -- so this covers the
                         # CPU reorder in GLESTexture.cpp as well as sampling.
                         --expect-textured 192,64,16
                         # The static-buffer quad: CreateBuffer, UpdateBuffer,
                         # CreateIndexBuffer, DrawBuffer -- the route world
                         # geometry takes, with attributes bound from the
                         # engine's own vertex-format tables.
                         --expect-static 96,176,48
                         # Generated GLSL, compiled by the real driver, drawn
                         # and read back. See RenderDll/XRenderGLES/
                         # GLESConform.cpp -- it covers the multi-stage case
                         # that the WGSL generator got wrong and that no
                         # assertion about emitted text could have caught.
                         --expect-shader-conformance)
        set_tests_properties(web_host_frames PROPERTIES TIMEOUT 300)
    endif()
endif()
