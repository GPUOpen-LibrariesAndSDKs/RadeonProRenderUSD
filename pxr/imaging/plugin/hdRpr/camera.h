#ifndef HDRPR_CAMERA_H
#define HDRPR_CAMERA_H

#include "pxr/imaging/hd/camera.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/range1f.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdRprCamera : public HdCamera {

public:
    HdRprCamera(SdfPath const& id);
    ~HdRprCamera() override = default;

    void Sync(HdSceneDelegate *sceneDelegate,
              HdRenderParam   *renderParam,
              HdDirtyBits     *dirtyBits) override;

    HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Finalize(HdRenderParam* renderParam) override;

    bool GetApertureSize(GfVec2f* value) const;
    bool GetApertureOffset(GfVec2f* value) const;
    bool GetFocalLength(float* value) const;
    bool GetFStop(float* value) const;
    bool GetFocusDistance(float* value) const;
    bool GetShutterOpen(double* value) const;
    bool GetShutterClose(double* value) const;
    bool GetClippingRange(GfRange1f* value) const;
    bool GetProjectionType(TfToken* value) const;

    HdDirtyBits GetDirtyBits() const { return m_rprDirtyBits; }
    void CleanDirtyBits() const { m_rprDirtyBits = HdCamera::Clean; }

private:
    float m_horizontalAperture;
    float m_verticalAperture;
    float m_horizontalApertureOffset;
    float m_verticalApertureOffset;
    float m_focalLength;
    float m_fStop;
    float m_focusDistance;
    double m_shutterOpen;
    double m_shutterClose;
    GfRange1f m_clippingRange;
    TfToken m_projectionType;

    mutable HdDirtyBits m_rprDirtyBits = HdCamera::AllDirty;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_CAMERA_H
