// Isolate HybridPro accumulation and teardown without Houdini or Hydra.
#include <RadeonProRender.h>
#include <RadeonProRender_Baikal.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#define CHECK(call) do { auto status = (call); if (status != RPR_SUCCESS) { \
    std::fprintf(stderr, "%s returned %d at line %d\n", #call, int(status), __LINE__); \
    throw std::runtime_error(#call); } } while (false)

static unsigned resolution = 128;

static int integer(const char* text, int minimum, int maximum) {
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(text, &end, 10);
    if (errno || end == text || *end || value < minimum || value > maximum)
        throw std::runtime_error("integer argument out of range");
    return int(value);
}

static rpr_shape quad(rpr_context context, const float* vertices, float normalY) {
    const float normals[] = {0, normalY, 0};
    const float uv[] = {0, 0, 1, 0, 1, 1, 0, 1};
    const int indices[] = {0, 1, 2, 0, 2, 3};
    const int normalIndices[] = {0, 0, 0, 0, 0, 0};
    const int faces[] = {3, 3};
    rpr_shape shape = nullptr;
    CHECK(rprContextCreateMesh(context, vertices, 4, 12, normals, 1, 12,
        uv, 4, 8, indices, 4, normalIndices, 4, indices, 4, faces, 2, &shape));
    return shape;
}

static rpr_shape sphere(rpr_context context, float x) {
    constexpr int segments = 64, rings = 32;
    constexpr double pi = 3.14159265358979323846;
    std::vector<float> vertices, normals, uv;
    std::vector<int> indices;
    for (int row = 0; row <= rings; ++row) {
        const double theta = pi * row / rings;
        for (int col = 0; col <= segments; ++col) {
            const double phi = 2 * pi * col / segments;
            const float nx = std::sin(theta) * std::cos(phi);
            const float ny = std::cos(theta);
            const float nz = std::sin(theta) * std::sin(phi);
            vertices.insert(vertices.end(), {x + .85f * nx, .86f + .85f * ny, .85f * nz});
            normals.insert(normals.end(), {nx, ny, nz});
            uv.insert(uv.end(), {float(col) / segments, float(row) / rings});
        }
    }
    for (int row = 0; row < rings; ++row) {
        for (int col = 0; col < segments; ++col) {
            const int a = row * (segments + 1) + col, b = a + segments + 1;
            if (row != 0) indices.insert(indices.end(), {a, a + 1, b});
            if (row != rings - 1) indices.insert(indices.end(), {a + 1, b + 1, b});
        }
    }
    const std::vector<int> faces(indices.size() / 3, 3);
    rpr_shape shape = nullptr;
    CHECK(rprContextCreateMesh(context, vertices.data(), vertices.size() / 3, 12,
        normals.data(), normals.size() / 3, 12, uv.data(), uv.size() / 2, 8,
        indices.data(), 4, indices.data(), 4, indices.data(), 4,
        faces.data(), faces.size(), &shape));
    return shape;
}

static void save(rpr_framebuffer fb, const std::string& path) {
    std::vector<float> values(size_t(resolution) * resolution * 4);
    CHECK(rprFrameBufferGetInfo(fb, RPR_FRAMEBUFFER_DATA,
        values.size() * sizeof(float), values.data(), nullptr));
    double sum = 0;
    for (size_t i = 0; i < values.size(); ++i) {
        if (!std::isfinite(values[i])) throw std::runtime_error("nonfinite output");
        if (i % 4 != 3) sum += values[i];
    }
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(float));
    if (!file) throw std::runtime_error("write failed");
    std::printf("%s RGB mean %.9g\n", path.c_str(), sum / (resolution * resolution * 3));
    std::fflush(stdout);
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 5) {
        std::fprintf(stderr, "Usage: %s /path/HybridPro.so output-prefix [variant] [GPU index 0..15]\n", argv[0]);
        return 2;
    }
    try {
        const std::string variant = argc >= 4 ? argv[3] : "steady";
        const std::vector<std::string> variants = {"steady", "batch", "set-iterations", "framecount",
            "svgf", "edits", "camera", "defer-delete", "keep-detached", "churn", "churn-immediate",
            "materials", "null-scene"};
        if (std::find(variants.begin(), variants.end(), variant) == variants.end())
            throw std::runtime_error("unknown variant");
        const int initialGpuIndex = argc == 5 ? integer(argv[4], 0, 15) : 0;
        if (const auto size = std::getenv("RPR_TEST_SIZE")) resolution = integer(size, 32, 2048);
        const auto cyclesText = std::getenv("RPR_TEST_CYCLES");
        const int cycles = cyclesText ? integer(cyclesText, 1, 20) : 3;
        const auto alternateText = std::getenv("RPR_TEST_ALTERNATE_GPU");
        const bool alternate = alternateText && integer(alternateText, 0, 1);
        if (alternate && initialGpuIndex > 1)
            throw std::runtime_error("alternation selects only GPU0/GPU1");
        const rpr_creation_flags gpuFlags[] = {
            RPR_CREATION_FLAGS_ENABLE_GPU0, RPR_CREATION_FLAGS_ENABLE_GPU1,
            RPR_CREATION_FLAGS_ENABLE_GPU2, RPR_CREATION_FLAGS_ENABLE_GPU3,
            RPR_CREATION_FLAGS_ENABLE_GPU4, RPR_CREATION_FLAGS_ENABLE_GPU5,
            RPR_CREATION_FLAGS_ENABLE_GPU6, RPR_CREATION_FLAGS_ENABLE_GPU7,
            RPR_CREATION_FLAGS_ENABLE_GPU8, RPR_CREATION_FLAGS_ENABLE_GPU9,
            RPR_CREATION_FLAGS_ENABLE_GPU10, RPR_CREATION_FLAGS_ENABLE_GPU11,
            RPR_CREATION_FLAGS_ENABLE_GPU12, RPR_CREATION_FLAGS_ENABLE_GPU13,
            RPR_CREATION_FLAGS_ENABLE_GPU14, RPR_CREATION_FLAGS_ENABLE_GPU15};
        const int plugin = rprRegisterPlugin(argv[1]);
        if (plugin < 0) throw std::runtime_error("register failed");
        for (int cycle = 0; cycle < cycles; ++cycle) {
            const int gpuIndex = alternate
                ? (initialGpuIndex + cycle) % 2 : initialGpuIndex;
            rpr_context context = nullptr;
            CHECK(rprCreateContext(RPR_API_VERSION, &plugin, 1,
                gpuFlags[gpuIndex],
                nullptr, "/tmp", &context));
            CHECK(rprContextSetActivePlugin(context, plugin));
            char name[1024] = {};
            CHECK(rprContextGetInfo(context, RPR_CONTEXT_GPU0_NAME, sizeof(name), name, nullptr));
            std::printf("cycle %d GPU %s\n", cycle, name); std::fflush(stdout);
            const auto expectedGpu = std::getenv("RPR_TEST_EXPECT_GPU");
            if (expectedGpu && !std::strstr(name, expectedGpu))
                throw std::runtime_error("unexpected device");
            CHECK(rprContextSetParameterByKey1u(context, RPR_CONTEXT_ITERATIONS, 1));
            CHECK(rprContextSetParameterByKey1u(context, RPR_CONTEXT_PT_DENOISER,
                variant == "svgf" ? RPR_DENOISER_SVGF : RPR_DENOISER_NONE));
            CHECK(rprContextSetParameterByKey1u(context, RPR_CONTEXT_UPSCALER, RPR_UPSCALER_NONE));
            CHECK(rprContextSetParameterByKey1u(context, RPR_CONTEXT_TONE_MAPPING, RPR_TONE_MAPPING_NONE));
            CHECK(rprContextSetParameterByKey1f(context, RPR_CONTEXT_DISPLAY_GAMMA, 1));
            rpr_scene scene = nullptr;
            CHECK(rprContextCreateScene(context, &scene));
            CHECK(rprContextSetScene(context, scene));
            rpr_camera camera = nullptr;
            CHECK(rprContextCreateCamera(context, &camera));
            if (variant == "materials") CHECK(rprCameraSetSensorSize(camera, 36, 36));
            CHECK(rprCameraLookAt(camera, 0, 2, 7, 0, 1, 0, 0, 1, 0));
            CHECK(rprSceneSetCamera(scene, camera));
            rpr_material_system system = nullptr;
            CHECK(rprContextCreateMaterialSystem(context, 0, &system));
            rpr_material_node diffuse = nullptr, emission = nullptr;
            CHECK(rprMaterialSystemCreateNode(system, RPR_MATERIAL_NODE_UBERV2, &diffuse));
            CHECK(rprMaterialNodeSetInputFByKey(diffuse, RPR_MATERIAL_INPUT_UBER_DIFFUSE_WEIGHT, 1, 1, 1, 1));
            CHECK(rprMaterialNodeSetInputFByKey(diffuse, RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR, .18, .18, .18, 1));
            CHECK(rprMaterialSystemCreateNode(system, RPR_MATERIAL_NODE_UBERV2, &emission));
            CHECK(rprMaterialNodeSetInputFByKey(emission, RPR_MATERIAL_INPUT_UBER_DIFFUSE_WEIGHT, 0, 0, 0, 0));
            CHECK(rprMaterialNodeSetInputFByKey(emission, RPR_MATERIAL_INPUT_UBER_EMISSION_WEIGHT, 1, 1, 1, 1));
            CHECK(rprMaterialNodeSetInputFByKey(emission, RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR, 30, 30, 30, 1));
            const float floorVertices[] = {-3, 0, -3, -3, 0, 3, 3, 0, 3, 3, 0, -3};
            const float lightVertices[] = {-2, 4, -.5, 2, 4, -.5, 2, 4, .5, -2, 4, .5};
            auto floor = quad(context, floorVertices, 1);
            auto light = quad(context, lightVertices, -1);
            CHECK(rprShapeSetMaterial(floor, diffuse));
            CHECK(rprShapeSetMaterial(light, emission));
            CHECK(rprSceneAttachShape(scene, floor));
            CHECK(rprSceneAttachShape(scene, light));
            std::vector<rpr_shape> materialShapes;
            std::vector<rpr_material_node> materialNodes;
            if (variant == "materials") {
                auto value = [](rpr_material_node node, rpr_material_node_input key, float v) {
                    CHECK(rprMaterialNodeSetInputFByKey(node, key, v, v, v, v));
                };
                rpr_material_node checker = nullptr, uvLookup = nullptr, scale = nullptr;
                CHECK(rprMaterialSystemCreateNode(system, RPR_MATERIAL_NODE_INPUT_LOOKUP, &uvLookup));
                CHECK(rprMaterialNodeSetInputUByKey(uvLookup, RPR_MATERIAL_INPUT_VALUE, RPR_MATERIAL_NODE_LOOKUP_UV));
                CHECK(rprMaterialSystemCreateNode(system, RPR_MATERIAL_NODE_ARITHMETIC, &scale));
                CHECK(rprMaterialNodeSetInputUByKey(scale, RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_MUL));
                CHECK(rprMaterialNodeSetInputNByKey(scale, RPR_MATERIAL_INPUT_COLOR0, uvLookup));
                CHECK(rprMaterialNodeSetInputFByKey(scale, RPR_MATERIAL_INPUT_COLOR1, 8, 8, 1, 1));
                CHECK(rprMaterialSystemCreateNode(system, RPR_MATERIAL_NODE_CHECKER_TEXTURE, &checker));
                CHECK(rprMaterialNodeSetInputNByKey(checker, RPR_MATERIAL_INPUT_UV, scale));
                CHECK(rprMaterialNodeSetInputNByKey(diffuse, RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR, checker));
                for (int i = 0; i < 2; ++i) {
                    rpr_material_node mat = nullptr;
                    CHECK(rprMaterialSystemCreateNode(system, RPR_MATERIAL_NODE_UBERV2, &mat));
                    value(mat, RPR_MATERIAL_INPUT_UBER_DIFFUSE_WEIGHT, 0);
                    value(mat, RPR_MATERIAL_INPUT_UBER_REFLECTION_WEIGHT, 1);
                    value(mat, RPR_MATERIAL_INPUT_UBER_REFLECTION_ROUGHNESS, i ? .01 : .12);
                    if (i == 0) {
                        CHECK(rprMaterialNodeSetInputUByKey(mat, RPR_MATERIAL_INPUT_UBER_REFLECTION_MODE, RPR_UBER_MATERIAL_IOR_MODE_METALNESS));
                        value(mat, RPR_MATERIAL_INPUT_UBER_REFLECTION_METALNESS, 1);
                        CHECK(rprMaterialNodeSetInputFByKey(mat, RPR_MATERIAL_INPUT_UBER_REFLECTION_COLOR, .95, .65, .2, 1));
                    } else {
                        CHECK(rprMaterialNodeSetInputUByKey(mat, RPR_MATERIAL_INPUT_UBER_REFLECTION_MODE, RPR_UBER_MATERIAL_IOR_MODE_PBR));
                        value(mat, RPR_MATERIAL_INPUT_UBER_REFLECTION_IOR, 1.5);
                        value(mat, RPR_MATERIAL_INPUT_UBER_REFLECTION_COLOR, 1);
                        value(mat, RPR_MATERIAL_INPUT_UBER_REFRACTION_WEIGHT, 1);
                        value(mat, RPR_MATERIAL_INPUT_UBER_REFRACTION_COLOR, 1);
                        value(mat, RPR_MATERIAL_INPUT_UBER_REFRACTION_IOR, 1.5);
                        value(mat, RPR_MATERIAL_INPUT_UBER_REFRACTION_ROUGHNESS, 0);
                        CHECK(rprMaterialNodeSetInputUByKey(mat, RPR_MATERIAL_INPUT_UBER_REFRACTION_THIN_SURFACE, 0));
                    }
                    auto shape = sphere(context, i ? 1 : -1);
                    CHECK(rprShapeSetMaterial(shape, mat));
                    CHECK(rprSceneAttachShape(scene, shape));
                    materialShapes.push_back(shape);
                    materialNodes.push_back(mat);
                }
                // Dependency owners precede inputs in explicit cleanup order.
                materialNodes.insert(materialNodes.end(), {checker, scale, uvLookup});
            }
            rpr_framebuffer fb = nullptr;
            const rpr_framebuffer_format format{4, RPR_COMPONENT_TYPE_FLOAT32};
            const rpr_framebuffer_desc dimensions{resolution, resolution};
            CHECK(rprContextCreateFrameBuffer(context, format, &dimensions, &fb));
            CHECK(rprContextSetAOV(context, RPR_AOV_COLOR, fb));
            CHECK(rprFrameBufferClear(fb));
            const std::string prefix = std::string(argv[2]) + "-" + std::to_string(cycle);
            auto render = [&](int count) {
                const auto start = std::chrono::steady_clock::now();
                if (variant == "batch") {
                    CHECK(rprContextSetParameterByKey1u(context, RPR_CONTEXT_ITERATIONS, count));
                    CHECK(rprContextRender(context));
                } else {
                    for (int sample = 0; sample < count; ++sample) {
                        if (variant == "set-iterations")
                            CHECK(rprContextSetParameterByKey1u(context, RPR_CONTEXT_ITERATIONS, 1));
                        if (variant == "framecount")
                            CHECK(rprContextSetParameterByKey1u(context, RPR_CONTEXT_FRAMECOUNT, sample));
                        CHECK(rprContextRender(context));
                    }
                }
                const double submitSeconds = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - start).count();
                std::vector<float> completed(size_t(resolution) * resolution * 4);
                CHECK(rprFrameBufferGetInfo(fb, RPR_FRAMEBUFFER_DATA,
                    completed.size() * sizeof(float), completed.data(), nullptr));
                std::printf("render %d samples at %ux%u: submit %.6f s; with readback %.6f s\n",
                    count, resolution, resolution, submitSeconds,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
            };
            render(32);
            save(fb, prefix + "-32.rgba32f");
            render(96);
            save(fb, prefix + "-128.rgba32f");
            CHECK(rprFrameBufferClear(fb));
            render(128);
            save(fb, prefix + "-reset128.rgba32f");
            rpr_shape retiredFloor = nullptr;
            if (variant == "edits" || variant == "camera" || variant == "defer-delete" || variant == "keep-detached" || variant == "materials") {
                CHECK(rprMaterialNodeSetInputFByKey(emission, RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR, 0, 0, 0, 1));
                CHECK(rprFrameBufferClear(fb));
                render(32);
                save(fb, prefix + "-lightoff32.rgba32f");
                if (variant != "camera" && variant != "materials") {
                    CHECK(rprSceneDetachShape(scene, floor));
                    if (variant == "defer-delete" || variant == "keep-detached") retiredFloor = floor;
                    else CHECK(rprObjectDelete(floor));
                    floor = quad(context, floorVertices, 1);
                    CHECK(rprShapeSetMaterial(floor, diffuse));
                    CHECK(rprSceneAttachShape(scene, floor));
                }
                CHECK(rprMaterialNodeSetInputFByKey(emission, RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR, 30, 30, 30, 1));
                CHECK(rprCameraLookAt(camera, 1, 2, 7, 0, 1, 0, 0, 1, 0));
                CHECK(rprFrameBufferClear(fb));
                render(128);
                save(fb, prefix + "-moved128.rgba32f");
                if (variant == "defer-delete") {
                    CHECK(rprObjectDelete(retiredFloor));
                    retiredFloor = nullptr;
                }
                CHECK(rprCameraLookAt(camera, 0, 2, 7, 0, 1, 0, 0, 1, 0));
                CHECK(rprFrameBufferClear(fb));
                render(128);
                save(fb, prefix + "-restored128.rgba32f");
            }
            if (variant == "churn" || variant == "churn-immediate") {
                // Rebuild emissive and diffuse geometry repeatedly in ONE live
                // scene. Releasing after one render/readback checks the actual
                // retirement boundary, not just retention until final teardown.
                for (int edit = 0; edit < 16; ++edit) {
                    auto oldFloor = floor;
                    auto oldLight = light;
                    auto oldDiffuse = diffuse;
                    auto oldEmission = emission;
                    CHECK(rprSceneDetachShape(scene, oldFloor));
                    CHECK(rprSceneDetachShape(scene, oldLight));
                    auto retire = [&]() {
                        CHECK(rprObjectDelete(oldFloor));
                        CHECK(rprObjectDelete(oldLight));
                        CHECK(rprObjectDelete(oldDiffuse));
                        CHECK(rprObjectDelete(oldEmission));
                    };
                    if (variant == "churn-immediate") retire();
                    CHECK(rprMaterialSystemCreateNode(system, RPR_MATERIAL_NODE_UBERV2, &diffuse));
                    CHECK(rprMaterialNodeSetInputFByKey(diffuse, RPR_MATERIAL_INPUT_UBER_DIFFUSE_WEIGHT, 1, 1, 1, 1));
                    CHECK(rprMaterialNodeSetInputFByKey(diffuse, RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR, .18, .18, .18, 1));
                    CHECK(rprMaterialSystemCreateNode(system, RPR_MATERIAL_NODE_UBERV2, &emission));
                    CHECK(rprMaterialNodeSetInputFByKey(emission, RPR_MATERIAL_INPUT_UBER_DIFFUSE_WEIGHT, 0, 0, 0, 0));
                    CHECK(rprMaterialNodeSetInputFByKey(emission, RPR_MATERIAL_INPUT_UBER_EMISSION_WEIGHT, 1, 1, 1, 1));
                    CHECK(rprMaterialNodeSetInputFByKey(emission, RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR, 30, 30, 30, 1));
                    floor = quad(context, floorVertices, 1);
                    light = quad(context, lightVertices, -1);
                    CHECK(rprShapeSetMaterial(floor, diffuse));
                    CHECK(rprShapeSetMaterial(light, emission));
                    CHECK(rprSceneAttachShape(scene, floor));
                    CHECK(rprSceneAttachShape(scene, light));
                    CHECK(rprCameraLookAt(camera, edit % 2, 2, 7, 0, 1, 0, 0, 1, 0));
                    CHECK(rprFrameBufferClear(fb));
                    render(1);
                    save(fb, prefix + "-edit" + std::to_string(edit) + "-1.rgba32f");
                    if (variant == "churn") retire();
                    render(31);
                    save(fb, prefix + "-edit" + std::to_string(edit) + "-32.rgba32f");
                }
            }
            std::puts("detach and delete"); std::fflush(stdout);
            CHECK(rprContextSetAOV(context, RPR_AOV_COLOR, nullptr));
            for (auto shape : materialShapes) {
                CHECK(rprSceneDetachShape(scene, shape));
                CHECK(rprObjectDelete(shape));
            }
            CHECK(rprSceneDetachShape(scene, light));
            CHECK(rprSceneDetachShape(scene, floor));
            CHECK(rprShapeSetMaterial(light, nullptr));
            CHECK(rprShapeSetMaterial(floor, nullptr));
            CHECK(rprObjectDelete(light));
            CHECK(rprObjectDelete(floor));
            if (retiredFloor) CHECK(rprObjectDelete(retiredFloor));
            CHECK(rprObjectDelete(emission));
            CHECK(rprObjectDelete(diffuse));
            for (auto node : materialNodes) CHECK(rprObjectDelete(node));
            CHECK(rprObjectDelete(system));
            CHECK(rprObjectDelete(fb));
            CHECK(rprSceneSetCamera(scene, nullptr));
            CHECK(rprObjectDelete(camera));
            // Current HybridPro dereferences null in rprContextSetScene. Its
            // bundled tutorial deletes the scene directly instead.
            if (variant == "null-scene") CHECK(rprContextSetScene(context, nullptr));
            CHECK(rprObjectDelete(scene));
            CHECK(rprObjectDelete(context));
            std::puts("context deleted"); std::fflush(stdout);
        }
        return 0;
    } catch (const std::exception& error) {
        // A failed GPU context is left to process cleanup; do not cascade SDK calls.
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
