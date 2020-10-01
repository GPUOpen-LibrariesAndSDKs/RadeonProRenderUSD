#include "arithmeticNode.h"
#include "nodeInfo.h"

#include "pxr/imaging/rprUsd/materialHelpers.h"
#include "pxr/imaging/rprUsd/materialMappings.h"
#include "pxr/base/tf/instantiateSingleton.h"
#include "pxr/base/arch/attributes.h"
#include "pxr/base/gf/matrix3f.h"

PXR_NAMESPACE_OPEN_SCOPE

template<typename T>
RprUsd_MaterialNode* RprUsd_CreateRprNode(
    RprUsd_MaterialBuilderContext* ctx,
    std::map<TfToken, VtValue> const& parameters) {
    return new T(ctx, parameters);
}

template <typename Node>
RprUsd_RprNodeInfo* GetNodeInfo() {
    auto ret = new RprUsd_RprNodeInfo;
    auto& nodeInfo = *ret;

    // Take `RPR_MATERIAL_NODE_OP_OPERATION`-like name and convert it into `operation`
    std::string name(Node::kOpName + sizeof("RPR_MATERIAL_NODE_OP"));
    for (size_t i = 0; i < name.size(); ++i) {
        name[i] = std::tolower(name[i], std::locale());
    }

    nodeInfo.name = "rpr_arithmetic_" + name;
    nodeInfo.uiName = std::string("RPR ") + Node::kUiName;
    nodeInfo.uiFolder = "Arithmetics";

    for (int i = 0; i < Node::kArity; ++i) {
        RprUsd_RprNodeInput input(RprUsd_RprNodeInput::kColor3);
        input.name = TfStringPrintf("color%d", i);
        input.uiName = TfStringPrintf("Color %d", i);
        input.valueString = "0,0,0";
        input.uiSoftMin = "0";
        input.uiSoftMax = "1";
        nodeInfo.inputs.push_back(std::move(input));
    }

    RprUsd_RprNodeOutput output(RprUsdMaterialNodeElement::kColor3);
    output.name = "out";
    output.uiName = "out";
    nodeInfo.outputs.push_back(std::move(output));

    return ret;
}

/// \class RprUsd_RprArithmeticNodeRegistry
///
/// Each arithmetic node is registered in RprUsd_RprArithmeticNodeRegistry.
/// In such a way we can easily create an arithmetic node for particular operation
/// from the code (e.g. RprUsd_UsdPreviewSurface, RprUsd_HoudiniPrincipledNode).
class RprUsd_RprArithmeticNodeRegistry {
public:
    static RprUsd_RprArithmeticNodeRegistry& GetInstance() {
        return TfSingleton<RprUsd_RprArithmeticNodeRegistry>::GetInstance();
    }

    template <typename T>
    void Register(rpr::MaterialNodeArithmeticOperation op) {
        if (m_lookupOp.count(op)) {
            TF_CODING_ERROR("Attempt to define the same arithmetic node twice: %u", op);
            return;
        }

        m_lookupOp[op] = m_descs.size();
        m_descs.emplace_back();
        auto& desc = m_descs.back();
        desc.factory = &RprUsd_CreateRprNode<T>;
        desc.info = GetNodeInfo<T>();

        // Register this node in the general node registry
        RprUsdMaterialRegistry::GetInstance().Register(
            TfToken(desc.info->name),
            desc.factory,
            desc.info);
    }

    RprUsd_RprArithmeticNode* Create(
        rpr::MaterialNodeArithmeticOperation op,
        RprUsd_MaterialBuilderContext* ctx,
        std::map<TfToken, VtValue> const& parameters) {
        auto it = m_lookupOp.find(op);
        if (it != m_lookupOp.end()) {
            return static_cast<RprUsd_RprArithmeticNode*>(m_descs[it->second].factory(ctx, parameters));
        }

        TF_CODING_ERROR("Unknown arithmetic node or not implemented: %u", op);
        return nullptr;
    }

private:
    friend class TfSingleton<RprUsd_RprArithmeticNodeRegistry>;

private:
    using FactoryFnc = std::function<RprUsd_MaterialNode*(
        RprUsd_MaterialBuilderContext*,
        std::map<TfToken, VtValue> const&)>;
    struct NodeDesc {
        RprUsd_RprNodeInfo* info;
        FactoryFnc factory;
    };

    std::vector<NodeDesc> m_descs;
    std::unordered_map<rpr::MaterialNodeArithmeticOperation, size_t> m_lookupOp;
};

TF_INSTANTIATE_SINGLETON(RprUsd_RprArithmeticNodeRegistry);

namespace {

/// There are roughly 42 arithmetic operations, each of them defines a C++ class.
/// The following define allows us to minimize code required to define these classes.
///
/// Depending on inputs, arithmetic node output can be calculated at time of
/// material graph construction or it will be calculated in RPR engine (rpr::MaterialNode).
/// The output can be calculated at the time of material graph construction when all inputs
/// are of trivial type (float, vectors, etc). If one of the inputs is a rpr::MaterialNode
/// then arithmetic node's output will be a rpr::MaterialNode.
/// This behavior allows to implement one uniform code-path for some complex logic over material
/// node inputs (for example, see Houdini's principled node implementation of emissive color).
#define DEFINE_ARITHMETIC_NODE(op, arity, eval, uiName, doc) \
class RprUsd_ ## op ## Node : public RprUsd_RprArithmeticNode { \
public: \
    RprUsd_ ## op ## Node( \
        RprUsd_MaterialBuilderContext* ctx, \
        std::map<TfToken, VtValue> const& parameters) : RprUsd_RprArithmeticNode(ctx) { \
            for (auto& entry : parameters) SetInput(entry.first, entry.second); \
        } \
    static constexpr int kArity = arity; \
    static constexpr rpr::MaterialNodeArithmeticOperation kOp = op; \
    static constexpr const char* kOpName = #op; \
    static constexpr const char* kUiName = uiName; \
    /*static constexpr const char* kDoc = doc;*/ \
protected: \
    int GetNumArguments() const final { return kArity; } \
    rpr::MaterialNodeArithmeticOperation GetOp() const final { return kOp; } \
    VtValue EvalOperation() const final { eval } \
}; \
ARCH_CONSTRUCTOR(RprUsd_InitArithmeticNode ## op, 255, void) { \
    RprUsd_RprArithmeticNodeRegistry::GetInstance().Register<RprUsd_ ## op ## Node>(op); \
}

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_SUB, 2, {
    return VtValue(GetRprFloat(m_args[0]) - GetRprFloat(m_args[1]));
}, "Subtraction", "Subtraction.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_ADD, 2, {
    return VtValue(GetRprFloat(m_args[0]) + GetRprFloat(m_args[1]));
}, "Addition", "Addition.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_MUL, 2, {
    return VtValue(GfCompMult(GetRprFloat(m_args[0]), GetRprFloat(m_args[1])));
}, "Multiplication", "Multiplication.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_DIV, 2, {
    auto lhs = GetRprFloat(m_args[0]);
    auto rhs = GetRprFloat(m_args[1]);
    decltype(lhs) out;
    for (size_t i = 0; i < out.dimension; ++i) {
        out[i] = lhs[i] / rhs[i];
    }
    return VtValue(out);
}, "Division", "Division.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_NORMALIZE3, 1, {
    auto in = GetRprFloat(m_args[0]);
    return VtValue(GfVec3f(in[0], in[1], in[2]).GetNormalized());
}, "Normalize", "Normalize output of color0");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_LENGTH3, 1, {
    auto in = GetRprFloat(m_args[0]);
    return VtValue(GfVec3f(in[0], in[1], in[2]).GetLength());
}, "Length", "Length of color0");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_DOT3, 2, {
    auto in0 = GetRprFloat(m_args[0]);
    auto in1 = GetRprFloat(m_args[1]);
    return VtValue(GfDot(GfVec3f(in0.data()), GfVec3f(in1.data())));
}, "Dot", "Dot product of two rgb vectors.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_DOT4, 2, {
    auto in0 = GetRprFloat(m_args[0]);
    auto in1 = GetRprFloat(m_args[1]);
    return VtValue(GfDot(in0, in1));
}, "Dot4", "Dot product of two rgba vectors.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_CROSS3, 2, {
    auto in0 = GetRprFloat(m_args[0]);
    auto in1 = GetRprFloat(m_args[1]);
    return VtValue(GfCross(GfVec3f(in0.data()), GfVec3f(in1.data())));
}, "Cross", "Cross product.");

#define PER_COMPONENT_UNARY_IMPL(mathFunc) \
    auto in = GetRprFloat(m_args[0]); \
    for (size_t i = 0; i < in.dimension; ++i) in[i] = mathFunc(in[i]); \
    return VtValue(in);

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_SIN, 1, {
    PER_COMPONENT_UNARY_IMPL(std::sin);
}, "Sin", "Trigometric sine (in radians).");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_COS, 1, {
    PER_COMPONENT_UNARY_IMPL(std::cos);
}, "Cos", "Trigometric cosine (in radians).");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_TAN, 1, {
    PER_COMPONENT_UNARY_IMPL(std::tan);
}, "Tan", "Trigometric tangent (in radians).");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_LOG, 1, {
    PER_COMPONENT_UNARY_IMPL(std::log);
}, "Log", "Log() function.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_ATAN, 1, {
    PER_COMPONENT_UNARY_IMPL(std::atan);
}, "Atan", "Trigometric arctangent (in radians).");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_ASIN, 1, {
    PER_COMPONENT_UNARY_IMPL(std::asin);
}, "Asin", "Trigometric arcsine (in radians).");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_ACOS, 1, {
    PER_COMPONENT_UNARY_IMPL(std::acos);
}, "Acos", "Trigometric arccosine (in radians).");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_ABS, 1, {
    PER_COMPONENT_UNARY_IMPL(std::abs);
}, "Abs", "Absolute value.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_FLOOR, 1, {
    PER_COMPONENT_UNARY_IMPL(std::floor);
}, "Floor", "Mathematical floor value of color0.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_AVERAGE_XYZ, 1, {
    auto in = GetRprFloat(m_args[0]);
    return VtValue(GfVec4f((in[0] + in[1] + in[2]) / 3.0f));
}, "Average XYZ", "Average of color0 RGB values.");

#define PER_COMPONENT_BINARY_IMPL(mathFunc) \
    auto in0 = GetRprFloat(m_args[0]); \
    auto in1 = GetRprFloat(m_args[1]); \
    decltype(in0) out; \
    for (size_t i = 0; i < in0.dimension; ++i) out[i] = mathFunc(in0[i], in1[i]); \
    return VtValue(out);

static float GetAverage(float v0, float v1) { return 0.5f * (v0 + v1); }

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_AVERAGE, 2, {
    PER_COMPONENT_BINARY_IMPL(GetAverage);
}, "Average", "Average of color0 and color1.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_MIN, 2, {
    PER_COMPONENT_BINARY_IMPL(std::min);
}, "Min", "Minimum of two inputs.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_MAX, 2, {
    PER_COMPONENT_BINARY_IMPL(std::max);
}, "Max", "Maximum of two inputs.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_MOD, 2, {
    PER_COMPONENT_BINARY_IMPL(std::fmod);
}, "Mod", "Modulus of two values.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_POW, 2, {
    PER_COMPONENT_BINARY_IMPL(std::pow);
}, "Pow", "Power (color0 ^ color1).");

#define LOGICAL_OPERATION_IMPL(OP) \
    auto lhs = GetRprFloat(m_args[0]); \
    auto rhs = GetRprFloat(m_args[1]); \
    decltype(lhs) out; \
    for (size_t i = 0; i < lhs.dimension; ++i) { \
        out[i] = (lhs[i] OP rhs[i]) ? 1.0f : 0.0f; \
    } \
    return VtValue(out);

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_LOWER_OR_EQUAL, 2, {
    LOGICAL_OPERATION_IMPL(<=);
}, "Lower or Equal", "Return 1 if color0 <= color1 else 0.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_LOWER, 2, {
    LOGICAL_OPERATION_IMPL(<);
}, "Lower", "Return 1 if color0 < color1 else 0.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_GREATER_OR_EQUAL, 2, {
    LOGICAL_OPERATION_IMPL(>=);
}, "Greater or Equal", "Return 1 if color0 >= color1 else 0.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_GREATER, 2, {
    LOGICAL_OPERATION_IMPL(>);
}, "Greater", "Return 1 if color0 > color1 else 0.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_EQUAL, 2, {
    LOGICAL_OPERATION_IMPL(==);
}, "Equal", "Return 1 if color0 == color1 else 0.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_NOT_EQUAL, 2, {
    LOGICAL_OPERATION_IMPL(!=);
}, "Not Equal", "Return 1 if color0 != color1 else 0.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_AND, 2, {
    LOGICAL_OPERATION_IMPL(&&);
}, "And", "Return 1 if color0 and color1 are not 0.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_OR, 2, {
    LOGICAL_OPERATION_IMPL(||);
}, "Or", "Return 1 if color0 or color1 are not 0");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_TERNARY, 3, {
    auto in0 = GetRprFloat(m_args[0]);
    auto in1 = GetRprFloat(m_args[1]);
    auto in2 = GetRprFloat(m_args[2]);
    decltype(in0) out;
    for (size_t i = 0; i < in0.dimension; ++i) {
        out[i] = in0[i] ? in1[i] : in2[i];
    }
    return VtValue(out);
}, "Ternary", "Return color1 if color0 is 0 else color2.");

#define SELECT_OPERATION_IMPL(component_index) \
    auto value = GetRprFloat(m_args[0]); \
    return VtValue(GfVec4f(value[component_index]));

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_SELECT_X, 1, {
    SELECT_OPERATION_IMPL(0);
}, "Select X", "Select the X component.");
DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_SELECT_Y, 1, {
    SELECT_OPERATION_IMPL(1);
}, "Select Y", "Select the Y component.");
DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_SELECT_Z, 1, {
    SELECT_OPERATION_IMPL(2);
}, "Select Z", "Select the Z component.");
DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_SELECT_W, 1, {
    SELECT_OPERATION_IMPL(3);
}, "Select W", "Select the W component.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_SHUFFLE_YZWX, 1, {
    auto in = GetRprFloat(m_args[0]);
    return VtValue(GfVec4f(in[1], in[2], in[3], in[0]));
}, "Shuffle YZWX", "Shuffle channels of color0.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_SHUFFLE_ZWXY, 1, {
    auto in = GetRprFloat(m_args[0]);
    return VtValue(GfVec4f(in[2], in[3], in[0], in[1]));
}, "Shuffle ZWXY", "Shuffle channels of color0.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_SHUFFLE_WXYZ, 1, {
    auto in = GetRprFloat(m_args[0]);
    return VtValue(GfVec4f(in[3], in[0], in[1], in[2]));
}, "Shuffle WXYZ", "Shuffle channels of color0.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_MAT_MUL, 4, {
    GfMatrix3f mat;
    for (size_t i = 0; i < mat.numRows; ++i) {
        auto input = GetRprFloat(m_args[i]);
        for (size_t j = 0; j < mat.numColumns; ++j) {
            mat[i][j] = input[j];
        }
    }

    auto input = GetRprFloat(m_args[3]);

    GfVec3f vec(input.data());
    return VtValue(mat * vec);
}, "Matrix multiply", "color0,1,2 make a 3x3 matrix, multiply by color3.");

DEFINE_ARITHMETIC_NODE(RPR_MATERIAL_NODE_OP_COMBINE, 4, {
    auto in0 = GetRprFloat(m_args[0]);
    auto in1 = GetRprFloat(m_args[1]);
    auto in2 = GetRprFloat(m_args[2]);
    decltype(in0) out(in0[0], in1[1], in2[2], 1.0f);

    if (!m_args[3].IsEmpty()) {
        auto in3 = GetRprFloat(m_args[3]);
        out[3] = in3[3];
    }
    return VtValue(out);
}, "Combine", "Combine to (color0.r, color1.g, color2.b, 1) with three inputs. Combine to (color0.r, color1.g, color2.b, color3.a) with four inputs.");

} // namespace anonymous

std::unique_ptr<RprUsd_RprArithmeticNode> RprUsd_RprArithmeticNode::Create(
    rpr::MaterialNodeArithmeticOperation operation,
    RprUsd_MaterialBuilderContext* ctx,
    std::map<TfToken, VtValue> const& parameters) {
    auto node = RprUsd_RprArithmeticNodeRegistry::GetInstance().Create(operation, ctx, parameters);
    return std::unique_ptr<RprUsd_RprArithmeticNode>(node);
}

bool RprUsd_RprArithmeticNode::SetInput(
    TfToken const& inputId,
    VtValue const& value) {
    int argIndex;
    if (inputId == RprUsdMaterialNodeInputTokens->color0) {
        argIndex = 0;
    } else if (inputId == RprUsdMaterialNodeInputTokens->color1) {
        argIndex = 1;
    } else if (inputId == RprUsdMaterialNodeInputTokens->color2) {
        argIndex = 2;
    } else if (inputId == RprUsdMaterialNodeInputTokens->color3) {
        argIndex = 3;
    } else {
        TF_CODING_ERROR("Unexpected input for arithmetic node: %s", inputId.GetText());
        return false;
    }

    return SetInput(argIndex, value);
}

bool RprUsd_RprArithmeticNode::SetInput(
    int index,
    VtValue const& value) {
    if (index < 0 || index > 3) {
        TF_CODING_ERROR("Invalid index: %d", index);
        return false;
    }

    // Output is invalidated when input changes
    m_output = VtValue();

    m_args[index] = value;
    return true;
}

VtValue RprUsd_RprArithmeticNode::GetOutput() {
    if (m_output.IsEmpty()) {
        // If all inputs are of trivial type (uint or float, GfVec3f, etc) we can
        // evaluate the output value on the CPU
        bool isInputsTrivial = true;

        int numArgs = GetNumArguments();
        for (int i = 0; i < numArgs; ++i) {
            if (m_args[i].IsHolding<RprMaterialNodePtr>()) {
                isInputsTrivial = false;
                break;
            }
        }

        if (isInputsTrivial) {
            m_output = EvalOperation();
        } else {
            // Otherwise, we setup rpr::MaterialNode that calculates the value in runtime
            RprMaterialNodePtr rprNode;

            rpr::Status status;
            rprNode.reset(m_ctx->rprContext->CreateMaterialNode(RPR_MATERIAL_NODE_ARITHMETIC, &status));

            if (rprNode) {
                if (RPR_ERROR_CHECK(rprNode->SetInput(RPR_MATERIAL_INPUT_OP, GetOp()), "Failed to set arithmetic node operation")) {
                    return m_output;
                }

                static const rpr::MaterialNodeInput s_arithmeticNodeInputs[] = {
                    RPR_MATERIAL_INPUT_COLOR0,
                    RPR_MATERIAL_INPUT_COLOR1,
                    RPR_MATERIAL_INPUT_COLOR2,
                    RPR_MATERIAL_INPUT_COLOR3,
                };
                for (int i = 0; i < numArgs; ++i) {
                    if (m_args[i].IsEmpty()) {
                        if (RPR_ERROR_CHECK(rprNode->SetInput(s_arithmeticNodeInputs[i], 0.0f, 0.0f, 0.0f, 0.0f), "Failed to set arithmetic node input")) {
                            return m_output;
                        }
                    } else {
                        if (SetRprInput(rprNode.get(), s_arithmeticNodeInputs[i], m_args[i]) != RPR_SUCCESS) {
                            return m_output;
                        }
                    }
                }

                m_output = VtValue(rprNode);
            } else {
                TF_RUNTIME_ERROR("%s", RPR_GET_ERROR_MESSAGE(status, "Failed to create arithmetic material node", m_ctx->rprContext).c_str());
            }
        }
    }

    return m_output;
}

PXR_NAMESPACE_CLOSE_SCOPE
