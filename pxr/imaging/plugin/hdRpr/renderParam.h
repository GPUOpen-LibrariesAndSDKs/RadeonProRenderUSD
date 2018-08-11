#ifndef HDRPR_RENDER_PARAM_H
#define HDRPR_RENDER_PARAM_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderDelegate.h"

class HdProRenderScene;

PXR_NAMESPACE_OPEN_SCOPE

///
/// \class HdProRenderRenderParam
///
/// The render delegate can create an object of type HdRenderParam, to pass
/// to each prim during Sync(). HdProRender uses this class to pass the
/// ProRender scene around.
/// 


class HdRprParam final : public HdRenderParam {
public:
	/*HdRprParam(RprApi * rprApi)
        : _rprApi(rprApi)
        {}*/
    virtual ~HdRprParam() = default;

	//RprApi * GetRprApi() const { return _rprApi; }

	//void SetRprApi(RprApi * rprApi) { _rprApi = rprApi;	}

private:
	//RprApi * _rprApi = nullptr;
};

typedef std::shared_ptr<HdRprParam> HdRprParamSharedPtr;

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDER_PARAM_H
