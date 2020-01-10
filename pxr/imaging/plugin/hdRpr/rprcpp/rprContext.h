#ifndef RPRCPP_CONTEXT_H
#define RPRCPP_CONTEXT_H

#include "rprObject.h"

#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

namespace rpr {

enum class PluginType : int
{
    NONE = -1,
    TAHOE = 0,
    HYBRID,
    FIRST = TAHOE,
    LAST = HYBRID
};

enum class RenderDeviceType
{
    NONE = -1,
    CPU = 0,
    GPU,
    FIRST = CPU,
    LAST = GPU
};

class Context : public Object {
public:
    static std::unique_ptr<Context> Create(PluginType plugin, RenderDeviceType renderDevice, bool enableGlInterop, char const* cachePath);

    ~Context() override = default;

    rpr_context GetHandle() const;
    bool IsGlInteropEnabled() const;
    PluginType GetActivePluginType() const;
    RenderDeviceType GetActiveRenderDeviceType() const;

private:
    Context() = default;

private:
    PluginType m_activePlugin = PluginType::NONE;
    RenderDeviceType m_renderDevice = RenderDeviceType::NONE;
    bool m_useGlInterop = false;
};

} // namespace rpr

PXR_NAMESPACE_CLOSE_SCOPE

#endif // RPRCPP_CONTEXT_H
