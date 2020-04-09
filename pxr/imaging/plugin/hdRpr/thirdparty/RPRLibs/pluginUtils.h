#pragma once

#include <pxr/base/vt/array.h>

// Wrapper around vdb grid that makes it convinient to pass data to RPR
template <typename GridT, typename LookupT, typename IndicesT>
class VDBGrid2RPR;

// for convinience
template <typename T>
using VDBGrid = VDBGrid2RPR<T, float, uint32_t>;

// Wrapper around vdb grid that makes it convinient to pass data to RPR
template <typename GridT, typename LookupT, typename IndicesT>
class VDBGrid2RPR
{
public:
    VDBGrid2RPR() : gridSizeX(0), gridSizeY(0), gridSizeZ(0) {}

    size_t gridSizeX;
    size_t gridSizeY;
    size_t gridSizeZ;

    PXR_INTERNAL_NS::VtArray<IndicesT> coords;
    PXR_INTERNAL_NS::VtArray<GridT> values;
    PXR_INTERNAL_NS::VtArray<LookupT> LUT;

    GridT maxValue;
    GridT minValue;

    bool IsValid(void) { return ((gridSizeX > 0) && (gridSizeY > 0) && (gridSizeZ > 0)); }
};
