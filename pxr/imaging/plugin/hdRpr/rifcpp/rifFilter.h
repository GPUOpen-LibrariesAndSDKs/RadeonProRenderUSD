#ifndef RIFCPP_FILTER_H
#define RIFCPP_FILTER_H

#include "rifContext.h"
#include "rifImage.h"

#include "boostIncludePath.h"
#include BOOST_INCLUDE_PATH(variant.hpp)

#include "pxr/base/gf/matrix4f.h"

#include <unordered_map>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace rif {

enum FilterInputType
{
    Color,
    Normal,
    Depth,
    WorldCoordinate,
    ObjectId,
    Trans,
    Albedo,
    MaxInput
};

using FilterParam = BOOST_NS::variant<int, float, std::string, GfMatrix4f>;

enum class FilterType
{
    None = -1,
    AIDenoise,
    Resample,
    EawDenoise,
    FIRST = AIDenoise,
    LAST = EawDenoise
};

class Filter {
public:
    static std::unique_ptr<Filter> Create(FilterType type, Context* rifContext, std::uint32_t width, std::uint32_t height);
    static std::unique_ptr<Filter> CreateCustom(rif_image_filter_type type, Context* rifContext);

    virtual ~Filter();

    void SetInput(FilterInputType inputType, rif_image rifImage, const float sigma);
    void SetInput(FilterInputType inputType, rpr::FrameBuffer* rprFrameBuffer, const float sigma);
    void SetOutput(rif_image rifImage);
    void SetOutput(rif_image_desc imageDesc);
    void SetOutput(rpr::FrameBuffer* rprFrameBuffer);
    void SetParam(const char* name, FilterParam param);

    rif_image GetOutput();

    void Update();

protected:
    Filter(Context* rifContext) : m_rifContext(rifContext) {}

    void DettachFilter();
    virtual void AttachFilter() = 0;

    void ApplyParameters();

protected:
    Context* m_rifContext;
    rif_image_filter m_rifFilter = nullptr;

    std::vector<rif_image_filter> m_auxFilters;
    std::vector<std::unique_ptr<Image>> m_auxImages;
    std::vector<std::unique_ptr<rif::Image>> m_retainedImages;

    struct InputTraits {
        rif_image rifImage;
        rpr::FrameBuffer* rprFrameBuffer;
        float sigma;
    };

    std::unordered_map<FilterInputType, InputTraits, std::hash<std::underlying_type<FilterInputType>::type>> m_inputs;
    std::unordered_map<std::string, FilterParam> m_params;

    rif_image m_outputImage = nullptr;

    enum ChangeTracker {
        Clean = 0,
        DirtyAll = ~0u,
        DirtyIOImage = 1 << 0,
        DirtyParameters = 1 << 2
    };
    uint32_t m_dirtyFlags = DirtyAll;

    bool m_isAttached = false;
};

} // namespace rif

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RIFCPP_FILTER_H
