#include "rprMtlxLoader.h"

#include <MaterialXFormat/Util.h> // mx::loadLibraries

#include <map>
#include <cstring>
#include <cstdarg>
#include <unordered_set>

namespace mx = MaterialX;

namespace {

const float kAcescgMatrix[] = {
    1.705079555511475, -0.6242334842681885, -0.0808461606502533,
    -0.1297005265951157, 1.138468623161316, -0.008768022060394287,
    -0.02416634373366833, -0.1246141716837883, 1.148780584335327
};

const mx::Vector3 kSrgbBreakPnt(0.03928571566939354, 0.03928571566939354, 0.03928571566939354);
const mx::Vector3 kSrgbSlope(0.07738015800714493, 0.07738015800714493, 0.07738015800714493);
const mx::Vector3 kSrgbScale(0.9478672742843628, 0.9478672742843628, 0.9478672742843628);
const mx::Vector3 kSrgbOffset(0.05213269963860512, 0.05213269963860512, 0.05213269963860512);
const mx::Vector3 kSrgbGamma(2.4, 2.4, 2.4);

const std::string SCALE_ATTRIBUTE = "scale";

//------------------------------------------------------------------------------
// Direct mappings of standard mtlx nodes to RPR nodes
//------------------------------------------------------------------------------

struct Mtlx2Rpr {
    struct Node {
        rpr_material_node_type id;
        std::map<std::string, rpr_material_node_input> inputs;
        using InputsValueType = decltype(inputs)::value_type;

        Node() = default;
        Node(rpr_material_node_type id, std::initializer_list<InputsValueType>&& inputs)
            : id(id), inputs(std::move(inputs)) {
        }
    };

    std::map<std::string, Node> nodes;
    std::map<std::string, rpr_material_node_arithmetic_operation> arithmeticOps;

    Mtlx2Rpr() {
        nodes["diffuse_brdf"] = {
            RPR_MATERIAL_NODE_MATX_DIFFUSE_BRDF, {
                {"color", RPR_MATERIAL_INPUT_COLOR},
                {"weight", RPR_MATERIAL_INPUT_WEIGHT},
                {"roughness", RPR_MATERIAL_INPUT_ROUGHNESS},
                {"normal", RPR_MATERIAL_INPUT_NORMAL},
            }
        };
        nodes["dielectric_brdf"] = {
            RPR_MATERIAL_NODE_MATX_DIELECTRIC_BRDF, {
                {"weight", RPR_MATERIAL_INPUT_WEIGHT},
                {"tint", RPR_MATERIAL_INPUT_TINT},
                {"ior", RPR_MATERIAL_INPUT_IOR},
                {"roughness", RPR_MATERIAL_INPUT_ROUGHNESS},
                {"normal", RPR_MATERIAL_INPUT_NORMAL},
                {"tangent", RPR_MATERIAL_INPUT_TANGENT},
                {"distribution", RPR_MATERIAL_INPUT_DISTRIBUTION},
                {"base", RPR_MATERIAL_INPUT_BASE},
            }
        };
        nodes["generalized_schlick_brdf"] = {
            RPR_MATERIAL_NODE_MATX_GENERALIZED_SCHLICK_BRDF, {
                {"color0", RPR_MATERIAL_INPUT_COLOR0},
                {"color90", RPR_MATERIAL_INPUT_COLOR1},
                {"exponent", RPR_MATERIAL_INPUT_EXPONENT},
                {"roughness", RPR_MATERIAL_INPUT_ROUGHNESS},
                {"weight", RPR_MATERIAL_INPUT_WEIGHT},
                {"normal", RPR_MATERIAL_INPUT_NORMAL},
                {"tangent", RPR_MATERIAL_INPUT_TANGENT},
                {"distribution", RPR_MATERIAL_INPUT_DISTRIBUTION},
                {"base", RPR_MATERIAL_INPUT_BASE},
            }
        };
        nodes["dielectric_btdf"] = {
            RPR_MATERIAL_NODE_MATX_DIELECTRIC_BTDF, {
                {"weight", RPR_MATERIAL_INPUT_WEIGHT},
                {"tint", RPR_MATERIAL_INPUT_COLOR},
                {"ior", RPR_MATERIAL_INPUT_IOR},
                {"roughness", RPR_MATERIAL_INPUT_ROUGHNESS},
                {"normal", RPR_MATERIAL_INPUT_NORMAL},
                {"tangent", RPR_MATERIAL_INPUT_TANGENT},
                {"distribution", RPR_MATERIAL_INPUT_DISTRIBUTION},
                {"interior", RPR_MATERIAL_INPUT_INTERIOR},
            }
        };
        nodes["sheen_brdf"] = {
            RPR_MATERIAL_NODE_MATX_SHEEN_BRDF, {
                {"weight", RPR_MATERIAL_INPUT_WEIGHT},
                {"color", RPR_MATERIAL_INPUT_COLOR},
                {"roughness", RPR_MATERIAL_INPUT_ROUGHNESS},
                {"normal", RPR_MATERIAL_INPUT_NORMAL},
                {"base", RPR_MATERIAL_INPUT_BASE},
            }
        };
        nodes["subsurface_brdf"] = {
            RPR_MATERIAL_NODE_MATX_SUBSURFACE_BRDF, {
                {"weight", RPR_MATERIAL_INPUT_WEIGHT},
                {"color", RPR_MATERIAL_INPUT_COLOR},
                {"radius", RPR_MATERIAL_INPUT_RADIUS},
                {"anisotropy", RPR_MATERIAL_INPUT_ANISOTROPIC},
                {"normal", RPR_MATERIAL_INPUT_NORMAL},
            }
        };
        nodes["diffuse_btdf"] = {
            RPR_MATERIAL_NODE_MATX_DIFFUSE_BTDF, {
                {"weight", RPR_MATERIAL_INPUT_WEIGHT},
                {"color", RPR_MATERIAL_INPUT_COLOR},
                {"normal", RPR_MATERIAL_INPUT_NORMAL},
            }
        };
        nodes["conductor_brdf"] = {
            RPR_MATERIAL_NODE_MATX_CONDUCTOR_BRDF, {
                {"weight", RPR_MATERIAL_INPUT_WEIGHT},
                {"reflectivity", RPR_MATERIAL_INPUT_REFLECTIVITY},
                {"edge_color", RPR_MATERIAL_INPUT_EDGE_COLOR},
                {"roughness", RPR_MATERIAL_INPUT_ROUGHNESS},
                {"normal", RPR_MATERIAL_INPUT_NORMAL},
                {"tangent", RPR_MATERIAL_INPUT_TANGENT},
                {"distribution", RPR_MATERIAL_INPUT_DISTRIBUTION},
            }
        };
        nodes["fresnel"] = {
            RPR_MATERIAL_NODE_MATX_FRESNEL, {
                {"ior", RPR_MATERIAL_INPUT_IOR},
                {"normal", RPR_MATERIAL_INPUT_NORMAL},
                {"viewdirection", RPR_MATERIAL_INPUT_VIEW_DIRECTION},
            }
        };
        nodes["constant"] = {
            RPR_MATERIAL_NODE_CONSTANT_TEXTURE, {
                {"value", RPR_MATERIAL_INPUT_VALUE},
            }
        };
        nodes["mix"] = {
            RPR_MATERIAL_NODE_BLEND_VALUE, {
                {"fg", RPR_MATERIAL_INPUT_COLOR1},
                {"bg", RPR_MATERIAL_INPUT_COLOR0},
                {"mix", RPR_MATERIAL_INPUT_WEIGHT},
            }
        };
        nodes["ifgreater"] = {
            RPR_MATERIAL_NODE_MATX_IFGREATER, {
                {"value1", RPR_MATERIAL_INPUT_0},
                {"value2", RPR_MATERIAL_INPUT_1},
                {"in1", RPR_MATERIAL_INPUT_COLOR0},
                {"in2", RPR_MATERIAL_INPUT_COLOR1},
            }
        };
        nodes["normalize"] = {
            RPR_MATERIAL_NODE_MATX_NORMALIZE, {
                {"in", RPR_MATERIAL_INPUT_COLOR},
            }
        };
        nodes["luminance"] = {
            RPR_MATERIAL_NODE_MATX_LUMINANCE, {
                {"in", RPR_MATERIAL_INPUT_0},
                {"lumacoeffs", RPR_MATERIAL_INPUT_LUMACOEFF},
            }
        };
        nodes["rotate3d"] = {
            RPR_MATERIAL_NODE_MATX_ROTATE3D, {
                {"in", RPR_MATERIAL_INPUT_0},
                {"amount", RPR_MATERIAL_INPUT_AMOUNT},
                {"axis", RPR_MATERIAL_INPUT_AXIS},
            }
        };
        nodes["roughness_anisotropy"] = {
            RPR_MATERIAL_NODE_MATX_ROUGHNESS_ANISOTROPY, {
                {"roughness", RPR_MATERIAL_INPUT_ROUGHNESS},
                {"anisotropy", RPR_MATERIAL_INPUT_ANISOTROPIC},
            }
        };
        nodes["noise3d"] = {
            RPR_MATERIAL_NODE_MATX_NOISE3D, {
                {"amplitude", RPR_MATERIAL_INPUT_AMPLITUDE},
                {"pivot", RPR_MATERIAL_INPUT_PIVOT},
                {"position", RPR_MATERIAL_INPUT_POSITION},
            }
        };
        nodes["normalmap"] = {
            RPR_MATERIAL_NODE_NORMAL_MAP, {
                {"in", RPR_MATERIAL_INPUT_COLOR},
                {"scale", RPR_MATERIAL_INPUT_SCALE},
            }
        };
        nodes["heighttonormal"] = {
            RPR_MATERIAL_NODE_BUMP_MAP, {
                {"in", RPR_MATERIAL_INPUT_COLOR},
                {"scale", RPR_MATERIAL_INPUT_SCALE},
            }
        };
        nodes["normalize"] = {
            RPR_MATERIAL_NODE_MATX_NORMALIZE, {
                {"in", RPR_MATERIAL_INPUT_COLOR},
            }
        };
        nodes["position"] = {RPR_MATERIAL_NODE_MATX_POSITION, {}};

        nodes["rpr_emissive"] = {
            RPR_MATERIAL_NODE_EMISSIVE, {
                {"color", RPR_MATERIAL_INPUT_COLOR}
            }
        };

        auto addArithmeticNode = [this](const char* name, rpr_material_node_arithmetic_operation op, int numArgs) {
            auto& mapping = nodes[name];
            mapping.id = RPR_MATERIAL_NODE_ARITHMETIC;

            arithmeticOps[name] = op;

            if (numArgs == 1) {
                mapping.inputs["in"] = RPR_MATERIAL_INPUT_COLOR0;
            } else {
                mapping.inputs["in1"] = RPR_MATERIAL_INPUT_COLOR0;
                mapping.inputs["in2"] = RPR_MATERIAL_INPUT_COLOR1;

                if (numArgs > 2) mapping.inputs["in3"] = RPR_MATERIAL_INPUT_COLOR2;
                if (numArgs > 3) mapping.inputs["in4"] = RPR_MATERIAL_INPUT_COLOR3;
            }
        };

        addArithmeticNode("sin", RPR_MATERIAL_NODE_OP_SIN, 1);
        addArithmeticNode("cos", RPR_MATERIAL_NODE_OP_COS, 1);
        addArithmeticNode("tan", RPR_MATERIAL_NODE_OP_TAN, 1);
        addArithmeticNode("asin", RPR_MATERIAL_NODE_OP_ASIN, 1);
        addArithmeticNode("acos", RPR_MATERIAL_NODE_OP_ACOS, 1);
        addArithmeticNode("absval", RPR_MATERIAL_NODE_OP_ABS, 1);
        addArithmeticNode("floor", RPR_MATERIAL_NODE_OP_FLOOR, 1);
        addArithmeticNode("ln", RPR_MATERIAL_NODE_OP_LOG, 1);
        addArithmeticNode("normalize", RPR_MATERIAL_NODE_OP_NORMALIZE3, 1);
        addArithmeticNode("add", RPR_MATERIAL_NODE_OP_ADD, 2);
        addArithmeticNode("subtract", RPR_MATERIAL_NODE_OP_SUB, 2);
        addArithmeticNode("multiply", RPR_MATERIAL_NODE_OP_MUL, 2);
        addArithmeticNode("divide", RPR_MATERIAL_NODE_OP_DIV, 2);
        addArithmeticNode("power", RPR_MATERIAL_NODE_OP_POW, 2);
        addArithmeticNode("min", RPR_MATERIAL_NODE_OP_MIN, 2);
        addArithmeticNode("max", RPR_MATERIAL_NODE_OP_MAX, 2);
        addArithmeticNode("dotproduct", RPR_MATERIAL_NODE_OP_DOT3, 2);
        addArithmeticNode("crossproduct", RPR_MATERIAL_NODE_OP_CROSS3, 2);
        addArithmeticNode("modulo", RPR_MATERIAL_NODE_OP_MOD, 2);

        arithmeticOps["invert"] = RPR_MATERIAL_NODE_OP_SUB;
        nodes["invert"] = {
            RPR_MATERIAL_NODE_ARITHMETIC, {
                {"amount", RPR_MATERIAL_INPUT_COLOR0},
                {"in", RPR_MATERIAL_INPUT_COLOR1},
            }
        };

        // TODO: add custom implementations
        arithmeticOps["clamp"] = RPR_MATERIAL_NODE_OP_MAX;
        nodes["clamp"] = {
            RPR_MATERIAL_NODE_ARITHMETIC, {
                {"in", RPR_MATERIAL_INPUT_COLOR0},
                {"low", RPR_MATERIAL_INPUT_COLOR1},
            }
        };
    }
};

Mtlx2Rpr const& GetMtlx2Rpr() {
    static Mtlx2Rpr s_mtlx2rpr;
    return s_mtlx2rpr;
}

//------------------------------------------------------------------------------
// Loader context declarations
//------------------------------------------------------------------------------

struct Node;
struct MtlxNodeGraphNode;
struct BaseConverterNode;

enum LogScope {
    LSGlobal = -1,
    LSGraph,
    LSNested = LSGraph,
    LSNode,
    LSInput,
    LogScopeMax
};

struct LoaderContext {
    mx::Document const* mtlxDocument;
    rpr_material_system rprMatSys;

    std::unique_ptr<MtlxNodeGraphNode> globalNodeGraph;
    Node* GetGlobalNode(mx::Node* mtlxNode);

    std::map<std::string, std::unique_ptr<MtlxNodeGraphNode>> freeStandingNodeGraphs;
    MtlxNodeGraphNode* GetFreeStandingNodeGraph(mx::NodeGraphPtr const& nodeGraph);

    std::map<std::string, std::unique_ptr<Node>> geomNodes;
    Node* GetGeomNode(mx::GeomPropDef* geomPropDef);

    template <typename T>
    bool ConnectToGlobalOutput(T* input, Node* node);

    mx::FileSearchPath searchPath;

    class ValueConverter {
    public:
        virtual ~ValueConverter() = default;

        /// Used to convert constant values
        virtual bool Convert(float color[4], LoaderContext* context) const { return false; }

        /// Used to convert color value computed in run-time
        virtual std::unique_ptr<BaseConverterNode> GetConversionNode(LoaderContext* context) const { return nullptr; }
    };
    std::map<std::string, std::unique_ptr<ValueConverter>> valueConvertersCache;
    ValueConverter* GetColorSpaceConverter(std::string const& colorspace);

    struct CompiledUnitType {
        std::map<std::string, float> scales;
        float sceneScale;

        float GetScale(std::string const& unit) {
            auto it = scales.find(unit);
            if (it != scales.end()) {
                return it->second;
            }
            return sceneScale;
        }

        float GetScale(std::string const& srcUnit, std::string const& dstUnit) {
            return GetScale(srcUnit) / GetScale(dstUnit);
        }
    };
    std::map<std::string, CompiledUnitType> compiledUnitTypes;
    std::unique_ptr<ValueConverter> GetUnitConverter(mx::Element* input);

    static const int kGlobalLogDepth = -1;
    int logDepth = kGlobalLogDepth;
    LogScope logScope = LSGlobal;

    RPRMtlxLoader::LogLevel logLevel;

    void Log(RPRMtlxLoader::LogLevel level, size_t line, const char* fmt, ...);

    struct ScopeGuard {
        LoaderContext* ctx;
        int previousLogDepth;
        LogScope previousLogScope;

        ScopeGuard(LoaderContext* ctx, LogScope logScope, mx::Element const* scopeElement);
        ~ScopeGuard();
    };
    ScopeGuard EnterScope(LogScope logScope, mx::Element const* scopeElement);

    std::string ResolveFile(std::string const& filename);
};

#define LOG_ERROR(ctx, fmt, ...) \
    (ctx)->Log(RPRMtlxLoader::LogLevel::Error, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_WARNING(ctx, fmt, ...) \
    (ctx)->Log(RPRMtlxLoader::LogLevel::Warning, __LINE__, fmt, ##__VA_ARGS__)

#define LOG(ctx, fmt, ...) \
    (ctx)->Log(RPRMtlxLoader::LogLevel::Info, __LINE__, fmt, ##__VA_ARGS__)

//------------------------------------------------------------------------------
// Node declarations
//------------------------------------------------------------------------------

struct Node {
    using Ptr = std::unique_ptr<Node>;

    virtual ~Node() = default;

    /// Connect the current (upstream) node to the \p downstreamNode
    virtual rpr_status Connect(std::string const& upstreamOutput, Node* downstreamNode, mx::TypedElement* downstreamElement, LoaderContext* context) = 0;

    /// Set input of the current (downstream) node to the output of upstream node
    virtual rpr_status SetInput(mx::TypedElement* downstreamElement, mx::Element* upstreamElement, rpr_material_node upstreamRprNode, LoaderContext* context) = 0;
    virtual rpr_status SetInput(mx::TypedElement* downstreamElement, mx::ValueElement* upstreamValueElement, LoaderContext* context) = 0;

    /// Some nodes might contain rpr_material_node handles on which the whole material is built,
    /// at the end of MaterialX processing we collect them into one linear array that is returned to the user.
    ///
    /// Returns number of nodes stored in the current node.
    /// Pass nullptr to \p dst to get only the number of nodes
    /// stored in the current node without actually moving them
    virtual size_t MoveRprApiHandles(rpr_material_node* dst) = 0;

    template <typename T>
    T* AsA() {
        return dynamic_cast<T*>(this);
    }

    static Node::Ptr Create(mx::Node* mtlxNode, LoaderContext* context);
};

/// The node that wraps rpr_material_node
///
struct RprNode : public Node {
    bool isOwningRprNode;
    rpr_material_node rprNode;

    /// When input connected to this node requires conversion (colorspace, units, etc),
    /// we create appropriate conversion node and retain it within this node
    std::map<std::string, std::unique_ptr<BaseConverterNode>> conversionNodes;

    /// \p retainNode controls whether RprNode owns \p node
    RprNode(rpr_material_node node, bool retainNode);
    ~RprNode() override;

    rpr_status Connect(std::string const& upstreamOutput, Node* downstreamNode, mx::TypedElement* downstreamElement, LoaderContext* context) override;
    rpr_status SetInput(mx::TypedElement* downstreamElement, mx::Element* upstreamElement, rpr_material_node upstreamRprNode, LoaderContext* context) override;
    rpr_status SetInput(mx::TypedElement* downstreamElement, mx::ValueElement* upstreamValueElement, LoaderContext* context) override;

    rpr_status SetInput(mx::TypedElement* downstreamElement, rpr_material_node_input downstreamRprId, mx::ValueElement* upstreamValueElement, LoaderContext* context);

    size_t MoveRprApiHandles(rpr_material_node* dst) override;
};

// The passthrough node transfers input unaltered
//
struct PassthroughNode : public RprNode {
    std::string inputName;
    PassthroughNode(std::string inputName) : RprNode(nullptr, false), inputName(std::move(inputName)) {}
    ~PassthroughNode() override = default;

    rpr_status SetInput(mx::TypedElement* downstreamElement, mx::Element* upstreamElement, rpr_material_node upstreamRprNode, LoaderContext* context) override {
        if (downstreamElement->getName() == inputName) {
            rprNode = upstreamRprNode;
            isOwningRprNode = false;
            return RPR_SUCCESS;
        } else {
            LOG(context, "Unsupported input: %s", downstreamElement->getName().c_str());
            return RPR_ERROR_UNSUPPORTED;
        }
    }

    rpr_status SetInput(mx::TypedElement* downstreamElement, mx::ValueElement* upstreamValueElement, LoaderContext* context) override {
        if (downstreamElement->getName() == inputName) {
            if (rprNode && isOwningRprNode) {
                rprObjectDelete(rprNode);
            }

            rprNode = nullptr;
            auto status = rprMaterialSystemCreateNode(context->rprMatSys, RPR_MATERIAL_NODE_CONSTANT_TEXTURE, &rprNode);
            if (status == RPR_SUCCESS) {
                status = RprNode::SetInput(downstreamElement, RPR_MATERIAL_INPUT_VALUE, upstreamValueElement, context);
                if (status == RPR_SUCCESS) {
                    isOwningRprNode = true;
                } else {
                    rprObjectDelete(rprNode);
                    rprNode = nullptr;
                }
            }
            return status;
        } else {
            LOG(context, "Unsupported input: %s", downstreamElement->getName().c_str());
            return RPR_ERROR_UNSUPPORTED;
        }
    }
};

struct DisplacementNode : public RprNode {
    DisplacementNode(LoaderContext* context) : RprNode(nullptr, true) {
        rprMaterialSystemCreateNode(context->rprMatSys, RPR_MATERIAL_NODE_ARITHMETIC, &rprNode);
        rprMaterialNodeSetInputUByKey(rprNode, RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_MUL);
    }
    ~DisplacementNode() override = default;

    rpr_status SetInput(mx::TypedElement* downstreamElement, mx::Element* upstreamElement, rpr_material_node upstreamRprNode, LoaderContext* context) override {
        if (downstreamElement->getName() == "displacement") {
            if (downstreamElement->getType() != "float") {
                LOG_ERROR(context, "Only scalar displacement is supported");
                return RPR_ERROR_UNSUPPORTED;
            }

            return rprMaterialNodeSetInputNByKey(rprNode, RPR_MATERIAL_INPUT_COLOR0, upstreamRprNode);
        } else if (downstreamElement->getName() == "scale") {
            return rprMaterialNodeSetInputNByKey(rprNode, RPR_MATERIAL_INPUT_COLOR1, upstreamRprNode);
        } else {
            LOG(context, "Unsupported input: %s", downstreamElement->getName().c_str());
            return RPR_ERROR_UNSUPPORTED;
        }
    }
    rpr_status SetInput(mx::TypedElement* downstreamElement, mx::ValueElement* upstreamValueElement, LoaderContext* context) override {
        if (downstreamElement->getName() == "displacement") {
            if (downstreamElement->getType() != "float") {
                LOG_ERROR(context, "Only scalar displacement is supported");
                return RPR_ERROR_UNSUPPORTED;
            }

            return RprNode::SetInput(downstreamElement, RPR_MATERIAL_INPUT_COLOR0, upstreamValueElement, context);
        } else if (downstreamElement->getName() == "scale") {
            return RprNode::SetInput(downstreamElement, RPR_MATERIAL_INPUT_COLOR1, upstreamValueElement, context);
        } else {
            LOG(context, "Unsupported input: %s", downstreamElement->getName().c_str());
            return RPR_ERROR_UNSUPPORTED;
        }
    }
};

/// The node that can be mapped (fully or partially) to MaterialX standard node
///
struct RprMappedNode : public RprNode {
    Mtlx2Rpr::Node const* rprNodeMapping;

    /// RprMappedNode takes ownership over \p node
    RprMappedNode(rpr_material_node node, Mtlx2Rpr::Node const* nodeMapping);
    ~RprMappedNode() override = default;

    rpr_status SetInput(mx::TypedElement* downstreamElement, mx::Element* upstreamElement, rpr_material_node upstreamRprNode, LoaderContext* context) override;
    rpr_status SetInput(mx::TypedElement* downstreamElement, mx::ValueElement* upstreamValueElement, LoaderContext* context) override;
};

struct RprImageNode : public RprMappedNode {
    mx::ValueElement* fileValueElement = nullptr;
    bool isFileDirty = true;
    std::string resolvedFilepath;

    // TODO: support frame ranges

    std::string type;
    std::string layer;
    mx::ValuePtr defaultValue;
    bool disableRprImageColorspace;

    /// Possible values: "constant", "clamp", "periodic", "mirror"
    std::string uaddressmode;
    std::string vaddressmode;

    /// Possible values: "closest", "linear", "cubic"
    // TODO: can we support this in RPR?
    //std::string filtertype;

    RprImageNode(std::string const& type, LoaderContext* context);
    ~RprImageNode() override = default;

    rpr_status Connect(std::string const& upstreamOutput, Node* downstreamNode, mx::TypedElement* downstreamElement, LoaderContext* context) override;
    rpr_status SetInput(mx::TypedElement* downstreamElement, mx::ValueElement* upstreamValueElement, LoaderContext* context) override;
};

struct RprUberNode : public RprMappedNode {
    RprUberNode(LoaderContext* context);
    ~RprUberNode() override = default;

    rpr_status SetInput(mx::TypedElement* downstreamElement, mx::ValueElement* upstreamValueElement, LoaderContext* context) override;
};

/// The node that wraps RprNode so that the latter one can be used as a surface material.
///
struct RprWrapNode : public RprNode {
    using Ptr = std::unique_ptr<RprWrapNode>;
   
    static bool IsOutputTypeSupported(std::string const& outputType);

    RprWrapNode(LoaderContext* ctx);
    ~RprWrapNode() override = default;

    rpr_status SetInput(mx::TypedElement* downstreamElement, mx::Element* upstreamElement, rpr_material_node upstreamRprNode, LoaderContext* context) override;
};

/// The node that wraps mx::GraphElement.
/// This node handles all type of connections possible in MaterialX graphs.
///
struct MtlxNodeGraphNode : public Node {
    mx::ConstGraphElementPtr mtlxGraph;
    std::map<std::string, Node::Ptr> subNodes;

    using Ptr = std::unique_ptr<MtlxNodeGraphNode>;

    struct NoOutputsError : public std::exception {};

    /// \p mtlxGraph may be either NodeGraph or Document,
    /// \p requiredOutputs specifies outputs that should be processed.
    /// Throws NoOutputsError exception if mtlxGraph has no outputs
    MtlxNodeGraphNode(mx::ConstGraphElementPtr const& mtlxGraph, std::vector<mx::OutputPtr> const& requiredOutputs, LoaderContext* context);

    /// Overload, requiredOutputs equals mtlxGraph->getOutputs().
    /// Throws NoOutputsError exception if mtlxGraph has no outputs
    MtlxNodeGraphNode(mx::ConstGraphElementPtr const& mtlxGraph, LoaderContext* context);

    enum LazyInit { LAZY_INIT };
    /// Constructs MtlxNodeGraphNode in which subNodes created on demand, \see GetSubNode
    MtlxNodeGraphNode(mx::ConstGraphElementPtr const& mtlxGraph, LazyInit);

    ~MtlxNodeGraphNode() override = default;

    Node* GetSubNode(std::string const& nodename, LoaderContext* context);

    rpr_status Connect(std::string const& upstreamOutput, Node* downstreamNode, mx::TypedElement* downstreamElement, LoaderContext* context) override;
    rpr_status SetInput(mx::TypedElement* downstreamElement, mx::Element* upstreamElement, rpr_material_node upstreamRprNode, LoaderContext* context) override;
    rpr_status SetInput(mx::TypedElement* downstreamElement, mx::ValueElement* upstreamValueElement, LoaderContext* context) override;

    size_t MoveRprApiHandles(rpr_material_node* dst) override { return 0; }

private:
    struct PendingConnection {
        mx::NodePtr upstreamNode;
        mx::OutputPtr upstreamNodeOutput;

        mx::NodePtr downstreamNode;
        mx::TypedElementPtr downstreamInput;
    };
    Node* _CreateSubNode(mx::NodePtr const& mtlxNode, LoaderContext* context);

    struct InterfaceSocket {
        mx::NodePtr subNode;
        mx::TypedElementPtr input;
    };
    std::map<std::string, std::vector<InterfaceSocket>> _interfaceSockets;

    struct InterfaceSocketsQuery {
        template <typename F>
        void ForEach(LoaderContext* context, F&& f) {
            if (_sockets) {
                for (auto& socket : *_sockets) {
                    auto nodeIt = _nodeGraphNode->subNodes.find(socket.subNode->getName());
                    if (nodeIt != _nodeGraphNode->subNodes.end()) {
                        LOG(context, " %s:%s", nodeIt->first.c_str(), socket.input->getName().c_str());

                        f(nodeIt->second.get(), socket.input.get());
                    }
                }
            }
        }

        explicit operator bool() const { return _sockets; }

        MtlxNodeGraphNode* _nodeGraphNode;
        std::vector<InterfaceSocket>* _sockets;
    };

    InterfaceSocketsQuery _GetInterfaceSockets(mx::TypedElement* downstreamElement);
    InterfaceSocketsQuery _GetInterfaceSockets(std::string const& interfaceName);
};

//------------------------------------------------------------------------------
// Utilities
//------------------------------------------------------------------------------

template<std::size_t N>
bool StringStartsWith(std::string const& string, const char(&prefix)[N]) {
    return std::strncmp(string.c_str(), prefix, N - 1) == 0;
}

mx::NodeGraphPtr GetNodeGraphImpl(mx::NodeDef* nodeDef) {
    static std::string universal("universal");
    if (auto impl = nodeDef->getImplementation(mx::EMPTY_STRING, universal)) {
        return impl->asA<mx::NodeGraph>();
    }
    return nullptr;
}

template <typename T>
std::shared_ptr<T> GetFirst(mx::Element const* element) {
    for (auto& child : element->getChildren()) {
        if (auto object = child->asA<T>()) {
            return object;
        }
    }
    return nullptr;
}

mx::OutputPtr GetOutput(mx::InterfaceElement const* interfaceElement, mx::PortElement* portElement, LoaderContext* context) {
    // If the interface element has a few outputs, the port element must specify a target output
    //
    if (interfaceElement->getType() == mx::MULTI_OUTPUT_TYPE_STRING) {
        auto& targetOutputName = portElement->getOutputString();
        if (targetOutputName.empty()) {
            LOG_ERROR(context, "invalid port element structure: output should be specified when connecting to multioutput element - port: %s, interface: %s", portElement->asString().c_str(), interfaceElement->asString().c_str());
            return nullptr;
        }

        auto output = interfaceElement->getOutput(targetOutputName);
        if (!output) {
            LOG_ERROR(context, "invalid connection: cannot determine output - %s", portElement->asString().c_str());
        }

        return output;
    }

    return GetFirst<mx::Output>(interfaceElement);
}

template <typename T>
size_t GetHash(T const& value) {
    return std::hash<T>{}(value);
}

//------------------------------------------------------------------------------
// Loader context implementation
//------------------------------------------------------------------------------

const char* const kLogLevelStr[int(RPRMtlxLoader::LogLevel::Info) + 1] = {
    "",
    "ERROR",
    "WARNING",
    "INFO",
};

void LoaderContext::Log(RPRMtlxLoader::LogLevel level, size_t line, const char* fmt, ...) {
    if (level > logLevel) {
        return;
    }

    printf("[MTLXLOADER %s] ", kLogLevelStr[int(level)]);

    if (logScope != LSGlobal) {
        int padding = 0;
        if (logDepth > 0) {
            padding += logDepth * LogScopeMax;
        }
        padding += logScope;

        if (padding > 0) {
            printf("%*s", padding, "");
        }
        printf("- ");
    }

    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    if (line) {
        printf(" (%zu)\n", line);
    }
}

LoaderContext::ScopeGuard::ScopeGuard(LoaderContext* ctx, LogScope logScope, mx::Element const* scopeElement)
    : ctx(ctx)
    , previousLogDepth(ctx->logDepth)
    , previousLogScope(ctx->logScope) {

    ctx->logScope = logScope;
    if (logScope == LSGlobal) {
        ctx->logDepth = kGlobalLogDepth;
    } else if (logScope == LSNested || logScope == LSNode) {
        ctx->logDepth++;
    }
}

LoaderContext::ScopeGuard::~ScopeGuard() {
    ctx->logScope = previousLogScope;
    ctx->logDepth = previousLogDepth;
}

LoaderContext::ScopeGuard LoaderContext::EnterScope(LogScope logScope, mx::Element const* scopeElement) {
    return ScopeGuard(this, logScope, scopeElement);
}

std::string LoaderContext::ResolveFile(std::string const& filename) {
    mx::FilePath filepath = searchPath.find(filename);
    if (filepath.isEmpty()) {
        return std::string();
    }
    return filepath.asString();
}

Node* LoaderContext::GetGlobalNode(mx::Node* node) {
    if (!globalNodeGraph) {
        auto docGraph = mtlxDocument->getSelf()->asA<mx::Document>();
        globalNodeGraph = std::make_unique<MtlxNodeGraphNode>(docGraph, MtlxNodeGraphNode::LAZY_INIT);
    }

    auto scope = EnterScope(LSGlobal, mtlxDocument);
    return globalNodeGraph->GetSubNode(node->getName(), this);
}

MtlxNodeGraphNode* LoaderContext::GetFreeStandingNodeGraph(mx::NodeGraphPtr const& nodeGraph) {
    auto it = freeStandingNodeGraphs.find(nodeGraph->getName());
    if (it == freeStandingNodeGraphs.end()) {
        MtlxNodeGraphNode::Ptr nodeGraphNode;
        try {
            nodeGraphNode = std::make_unique<MtlxNodeGraphNode>(nodeGraph, this);
        } catch (MtlxNodeGraphNode::NoOutputsError& e) {
            // Store nullptr nodeGraph in freeStandingNodeGraphs to not fall into the same failure
        }
        it = freeStandingNodeGraphs.emplace(nodeGraph->getName(), std::move(nodeGraphNode)).first;
    }

    return it->second.get();
}

template <typename T>
bool LoaderContext::ConnectToGlobalOutput(T* input, Node* node) {
    auto& outputName = input->getOutputString();
    if (outputName.empty()) {
        return false;
    }

    // The output attribute may point to the output of some free-standing node graph or free-standing output
    //
    auto& nodeGraphName = input->getAttribute(mx::PortElement::NODE_GRAPH_ATTRIBUTE);
    if (!nodeGraphName.empty()) {
        if (auto nodeGraph = mtlxDocument->getNodeGraph(nodeGraphName)) {
            if (auto nodeGraphOutput = nodeGraph->getOutput(outputName)) {
                if (auto freeStandingNodeGraphNode = GetFreeStandingNodeGraph(nodeGraph)) {
                    LOG(this, "Bindinput %s: %s:%s (nodegraph)", input->getName().c_str(), nodeGraphName.c_str(), outputName.c_str());

                    return freeStandingNodeGraphNode->Connect(nodeGraphOutput->getName(), node, input, this) == RPR_SUCCESS;
                }
            }
        }
    } else {
        // A global output is an output that is defined globally in mtlxDocument
        //
        if (auto globalOutput = mtlxDocument->getOutput(outputName)) {
            // Such outputs point to a globally instantiated nodes
            //
            if (auto mtlxGlobalNode = globalOutput->getConnectedNode()) {
                if (auto mxtlGlobalNodeDef = mtlxGlobalNode->getNodeDef()) {
                    if (auto mtlxGlobalNodeOutput = GetOutput(mxtlGlobalNodeDef.get(), globalOutput.get(), this)) {
                        if (auto globalNode = GetGlobalNode(mtlxGlobalNode.get())) {
                            LOG(this, "Bindinput %s: %s (output)", input->getName().c_str(), outputName.c_str());

                            return globalNode->Connect(mtlxGlobalNodeOutput->getName(), node, input, this) == RPR_SUCCESS;
                        }
                    }
                }
            }
        }
    }

    return false;
}

Node* LoaderContext::GetGeomNode(mx::GeomPropDef* geomPropDef) {
    auto geomNodeIt = geomNodes.find(geomPropDef->getName());
    if (geomNodeIt != geomNodes.end()) {
        return geomNodeIt->second.get();
    }

    auto& geomProp = geomPropDef->getGeomProp();
    auto& type = geomPropDef->getAttribute("type");
    if (geomProp.empty() || type.empty()) {
        LOG_ERROR(this, "Invalid geomPropDef: %s", geomPropDef->asString().c_str());
        return nullptr;
    }

    rpr_material_node apiHandle = nullptr;

    if (geomProp == "tangent") {
        auto& space = geomPropDef->getSpace();
        if (space == "world") {
            auto status = rprMaterialSystemCreateNode(rprMatSys, RPR_MATERIAL_NODE_MATX_TANGENT, &apiHandle);
            if (!apiHandle) {
                LOG_ERROR(this, "Failed to create matx tangent node: %d", status);
            }
        } else {
            LOG_ERROR(this, "Unsupported tangent space: \"%s\"", space.c_str());
        }
    } else {
        const auto kInvalidLookupValue = static_cast<rpr_material_node_lookup_value>(-1);
        rpr_material_node_lookup_value lookupValue = kInvalidLookupValue;

        if (geomProp == "texcoord") {
            if (type != "vector2") {
                LOG_ERROR(this, "Unexpected type for texcoord geomProp: %s", type.c_str());
            }

            auto& index = geomPropDef->getIndex();
            if (index.empty() || index == "0") {
                lookupValue = RPR_MATERIAL_NODE_LOOKUP_UV;
            } else if (index == "1") {
                lookupValue = RPR_MATERIAL_NODE_LOOKUP_UV1;
            }
        } else if (geomProp == "normal") {
            auto& space = geomPropDef->getSpace();
            if (space == "world") {
                lookupValue = RPR_MATERIAL_NODE_LOOKUP_N;
            } else {
                LOG_ERROR(this, "Unsupported normal space: \"%s\"", space.c_str());
            }
        } else if (geomProp == "position") {
            auto& space = geomPropDef->getSpace();
            if (space == "world") {
                lookupValue = RPR_MATERIAL_NODE_LOOKUP_P;
            } else {
                lookupValue = RPR_MATERIAL_NODE_LOOKUP_P_LOCAL;
            }
        }
        // TODO: handle bitangent, geomcolor, geompropvalue (primvar)

        if (lookupValue != kInvalidLookupValue) {
            auto status = rprMaterialSystemCreateNode(rprMatSys, RPR_MATERIAL_NODE_INPUT_LOOKUP, &apiHandle);
            if (apiHandle) {
                rprMaterialNodeSetInputUByKey(apiHandle, RPR_MATERIAL_INPUT_VALUE, lookupValue);
            } else {
                LOG_ERROR(this, "Failed to create RPR_MATERIAL_NODE_INPUT_LOOKUP node: %d", status);
            }
        }
    }

    if (apiHandle) {
        auto geomNodeIt = geomNodes.emplace(geomPropDef->getName(), std::make_unique<RprNode>(apiHandle, true)).first;
        return geomNodeIt->second.get();
    }

    LOG_ERROR(this, "Unsupported geom node: %s", geomPropDef->asString().c_str());
    return nullptr;
}

struct BaseConverterNode {
    virtual ~BaseConverterNode() = default;

    virtual rpr_status SetInput(rpr_material_node inputNode, LoaderContext* context) = 0;
    virtual rpr_material_node GetOutput() = 0;

    virtual size_t MoveRprApiHandles(rpr_material_node* dst) = 0;
};

LoaderContext::ValueConverter* LoaderContext::GetColorSpaceConverter(std::string const& colorspace) {
    if (colorspace.empty() ||
        colorspace == "none") {
        return nullptr;
    }

    auto it = valueConvertersCache.find(colorspace);
    if (it != valueConvertersCache.end()) {
        return it->second.get();
    }

    std::unique_ptr<ValueConverter> converter;
    if (StringStartsWith(colorspace, "gamma")) {
        std::string gammaStr = colorspace.substr(sizeof("gamma") - 1);
        float gamma = std::stof(gammaStr) * 0.1f;

        /// Applies gamma decoding - pow(color, gamma)
        struct GammaConverter : public ValueConverter {
            float gamma;

            GammaConverter(float gamma) : gamma(gamma) {}
            ~GammaConverter() override = default;

            bool Convert(float color[3], LoaderContext* context) const override {
                for (int i = 0; i < 3; ++i) {
                    float value = std::max(color[i], 0.0f);
                    color[i] = std::pow(value, gamma);
                }
                return true;
            }

            std::unique_ptr<BaseConverterNode> GetConversionNode(LoaderContext* context) const override {
                struct GammaConversioNode : public BaseConverterNode {
                    rpr_material_node powNode;

                    GammaConversioNode(float gamma, LoaderContext* context) {
                        rprMaterialSystemCreateNode(context->rprMatSys, RPR_MATERIAL_NODE_ARITHMETIC, &powNode);
                        rprMaterialNodeSetInputUByKey(powNode, RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_POW);
                        rprMaterialNodeSetInputFByKey(powNode, RPR_MATERIAL_INPUT_COLOR1, gamma, gamma, gamma, 1.0f);
                    }

                    ~GammaConversioNode() override {
                        if (powNode) {
                            rprObjectDelete(powNode);
                        }
                    }

                    rpr_status SetInput(rpr_material_node inputNode, LoaderContext* context) override {
                        return rprMaterialNodeSetInputNByKey(powNode, RPR_MATERIAL_INPUT_COLOR0, inputNode);
                    }

                    rpr_material_node GetOutput() override {
                        return powNode;
                    }

                    size_t MoveRprApiHandles(rpr_material_node* dst) override {
                        if (powNode) {
                            if (dst) {
                                *dst = powNode;
                                powNode = nullptr;
                            }
                            return 1;
                        }

                        return 0;
                    }
                };

                return std::make_unique<GammaConversioNode>(gamma, context);
            }
        };
        converter = std::make_unique<GammaConverter>(gamma);
    } else if (colorspace == "acescg") {
        /// Multiplies input color on kAcescgMatrix
        struct AcescgConverter : public ValueConverter {
            ~AcescgConverter() override = default;

            bool Convert(float color[3], LoaderContext* context) const override {
                float out[3];
                for (int col = 0; col < 3; ++col) {
                    out[col] = 0.0f;
                    for (int i = 0; i < 3; ++i) {
                        out[col] += color[i] * kAcescgMatrix[col * 3 + i];
                    }
                }
                for (int i = 0; i < 3; ++i) {
                    color[i] = out[i];
                }
                return true;
            }

            std::unique_ptr<BaseConverterNode> GetConversionNode(LoaderContext* context) const override {
                struct AcescgConversioNode : public BaseConverterNode {
                    rpr_material_node matMulNode;

                    AcescgConversioNode(LoaderContext* context) {
                        rprMaterialSystemCreateNode(context->rprMatSys, RPR_MATERIAL_NODE_ARITHMETIC, &matMulNode);
                        rprMaterialNodeSetInputUByKey(matMulNode, RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_MAT_MUL);
                        rprMaterialNodeSetInputFByKey(matMulNode, RPR_MATERIAL_INPUT_COLOR0, kAcescgMatrix[0], kAcescgMatrix[1], kAcescgMatrix[2], 0.0f);
                        rprMaterialNodeSetInputFByKey(matMulNode, RPR_MATERIAL_INPUT_COLOR1, kAcescgMatrix[3], kAcescgMatrix[4], kAcescgMatrix[5], 0.0f);
                        rprMaterialNodeSetInputFByKey(matMulNode, RPR_MATERIAL_INPUT_COLOR2, kAcescgMatrix[6], kAcescgMatrix[7], kAcescgMatrix[8], 0.0f);
                    }

                    ~AcescgConversioNode() override {
                        if (matMulNode) {
                            rprObjectDelete(matMulNode);
                        }
                    }

                    rpr_status SetInput(rpr_material_node inputNode, LoaderContext* context) override {
                        return rprMaterialNodeSetInputNByKey(matMulNode, RPR_MATERIAL_INPUT_COLOR3, inputNode);
                    }

                    rpr_material_node GetOutput() override {
                        return matMulNode;
                    }

                    size_t MoveRprApiHandles(rpr_material_node* dst) override {
                        if (matMulNode) {
                            if (dst) {
                                *dst = matMulNode;
                                matMulNode = nullptr;
                            }
                            return 1;
                        }

                        return 0;
                    }
                };

                return std::make_unique<AcescgConversioNode>(context);
            }
        };
        converter = std::make_unique<AcescgConverter>();
    } else if (colorspace == "srgb_texture") {
        /// Applies sRGB to Linear conversion
        struct SrgbConverter : public ValueConverter {
            ~SrgbConverter() override = default;

            bool Convert(float color[3], LoaderContext* context) const override {
                for (int i = 0; i < 3; i++) {
                    if (color[i] > kSrgbBreakPnt[i]) {
                        color[i] = std::pow(std::max(0.0f, kSrgbScale[i] * color[i] + kSrgbOffset[i]), kSrgbGamma[i]);
                    } else {
                        color[i] = color[i] * kSrgbSlope[i];
                    }
                }
                return true;
            }

            std::unique_ptr<BaseConverterNode> GetConversionNode(LoaderContext* context) const override {
                struct SrgbConversionNode : public BaseConverterNode {
                    enum NodeTypes {
                        kIsAboveBreak,
                        kLinSeg,
                        kMax,
                        kScale,
                        kOffset,
                        kPowSeg,
                        kOut,
                        kTotalNodes
                    };
                    rpr_material_node nodes[kTotalNodes];

                    SrgbConversionNode(LoaderContext* context) {
                        auto createArithmeticNode = [context](rpr_material_node_arithmetic_operation op) {
                            rpr_material_node node;
                            rprMaterialSystemCreateNode(context->rprMatSys, RPR_MATERIAL_NODE_ARITHMETIC, &node);
                            rprMaterialNodeSetInputUByKey(node, RPR_MATERIAL_INPUT_OP, op);
                            return node;
                        };

                        nodes[kLinSeg] = createArithmeticNode(RPR_MATERIAL_NODE_OP_MUL);
                        // set RPR_MATERIAL_INPUT_COLOR0 to the input color
                        rprMaterialNodeSetInputFByKey(nodes[kLinSeg], RPR_MATERIAL_INPUT_COLOR1, kSrgbSlope[0], kSrgbSlope[1], kSrgbSlope[2], 0.0f);

                        nodes[kScale] = createArithmeticNode(RPR_MATERIAL_NODE_OP_MUL);
                        // set RPR_MATERIAL_INPUT_COLOR0 to the input color
                        rprMaterialNodeSetInputFByKey(nodes[kScale], RPR_MATERIAL_INPUT_COLOR1, kSrgbScale[0], kSrgbScale[1], kSrgbScale[2], 0.0f);

                        nodes[kOffset] = createArithmeticNode(RPR_MATERIAL_NODE_OP_ADD);
                        rprMaterialNodeSetInputNByKey(nodes[kOffset], RPR_MATERIAL_INPUT_COLOR0, nodes[kScale]);
                        rprMaterialNodeSetInputFByKey(nodes[kOffset], RPR_MATERIAL_INPUT_COLOR1, kSrgbOffset[0], kSrgbOffset[1], kSrgbOffset[2], 0.0f);

                        nodes[kMax] = createArithmeticNode(RPR_MATERIAL_NODE_OP_MAX);
                        rprMaterialNodeSetInputNByKey(nodes[kMax], RPR_MATERIAL_INPUT_COLOR0, nodes[kOffset]);
                        rprMaterialNodeSetInputFByKey(nodes[kMax], RPR_MATERIAL_INPUT_COLOR1, 0.0f, 0.0f, 0.0f, 0.0f);

                        nodes[kPowSeg] = createArithmeticNode(RPR_MATERIAL_NODE_OP_POW);
                        rprMaterialNodeSetInputNByKey(nodes[kPowSeg], RPR_MATERIAL_INPUT_COLOR0, nodes[kMax]);
                        rprMaterialNodeSetInputFByKey(nodes[kPowSeg], RPR_MATERIAL_INPUT_COLOR1, kSrgbGamma[0], kSrgbGamma[1], kSrgbGamma[2], 1.0f);

                        nodes[kIsAboveBreak] = createArithmeticNode(RPR_MATERIAL_NODE_OP_GREATER);
                        // set RPR_MATERIAL_INPUT_COLOR0 to the input color
                        rprMaterialNodeSetInputFByKey(nodes[kIsAboveBreak], RPR_MATERIAL_INPUT_COLOR1, kSrgbBreakPnt[0], kSrgbBreakPnt[1], kSrgbBreakPnt[2], 1.0f);

                        rprMaterialSystemCreateNode(context->rprMatSys, RPR_MATERIAL_NODE_BLEND_VALUE, &nodes[kOut]);
                        rprMaterialNodeSetInputNByKey(nodes[kOut], RPR_MATERIAL_INPUT_COLOR0, nodes[kPowSeg]);
                        rprMaterialNodeSetInputNByKey(nodes[kOut], RPR_MATERIAL_INPUT_COLOR1, nodes[kLinSeg]);
                        rprMaterialNodeSetInputNByKey(nodes[kOut], RPR_MATERIAL_INPUT_WEIGHT, nodes[kIsAboveBreak]);
                    }

                    rpr_status SetInput(rpr_material_node inputNode, LoaderContext* context) override {
                        rpr_status status;
                        if ((status = rprMaterialNodeSetInputNByKey(nodes[kLinSeg], RPR_MATERIAL_INPUT_COLOR0, inputNode)) != RPR_SUCCESS ||
                            (status = rprMaterialNodeSetInputNByKey(nodes[kScale], RPR_MATERIAL_INPUT_COLOR0, inputNode)) != RPR_SUCCESS ||
                            (status = rprMaterialNodeSetInputNByKey(nodes[kIsAboveBreak], RPR_MATERIAL_INPUT_COLOR0, inputNode)) != RPR_SUCCESS) {
                            return status;
                        }

                        return RPR_SUCCESS;
                    }

                    rpr_material_node GetOutput() override {
                        return nodes[kOut];
                    }

                    size_t MoveRprApiHandles(rpr_material_node* dst) override {
                        if (dst) {
                            std::memcpy(dst, nodes, sizeof(nodes));
                            std::memset(nodes, 0, sizeof(nodes));
                        }
                        return kTotalNodes;
                    }
                };

                return std::make_unique<SrgbConversionNode>(context);
            }
        };
        converter = std::make_unique<SrgbConverter>();
    } else if (colorspace == "lin_rec709") {
        // no-op?
    } else {
        LOG_ERROR(this, "Unknown colorspace");
    }

    if (!converter) {
        return nullptr;
    }

    it = valueConvertersCache.emplace(colorspace, std::move(converter)).first;
    return it->second.get();
}

std::unique_ptr<LoaderContext::ValueConverter> LoaderContext::GetUnitConverter(mx::Element* inputElement) {
    if (!inputElement) {
        return {};
    }

    auto input = inputElement->asA<mx::ValueElement>();
    if (!input) {
        return {};
    }

    auto& srcUnitSpace = input->getUnit();
    if (srcUnitSpace.empty()) {
        return {};
    }

    auto& unitType = input->getUnitType();

    auto it = compiledUnitTypes.find(unitType);
    if (it == compiledUnitTypes.end()) {
        LOG_ERROR(this, "Unknown unitType: %s", unitType.c_str());
        return {};
    }

    auto& dstUnitSpace = input->getActiveUnit();

    float scale = it->second.GetScale(srcUnitSpace, dstUnitSpace);
    if (std::abs(scale - 1.0f) < 1e-6f) {
        // no-op
        return {};
    }

    struct ScaleConverter : public ValueConverter {
        float scale;

        ScaleConverter(float scale) : scale(scale) {}
        ~ScaleConverter() override = default;

        bool Convert(float color[4], LoaderContext* context) const override {
            for (int i = 0; i < 4; ++i) {
                color[i] *= scale;
            }
            return true;
        }

        std::unique_ptr<BaseConverterNode> GetConversionNode(LoaderContext* context) const override {
            struct ScaleConversioNode : public BaseConverterNode {
                rpr_material_node mulNode;

                ScaleConversioNode(float scale, LoaderContext* context) {
                    rprMaterialSystemCreateNode(context->rprMatSys, RPR_MATERIAL_NODE_ARITHMETIC, &mulNode);
                    rprMaterialNodeSetInputUByKey(mulNode, RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_MUL);
                    rprMaterialNodeSetInputFByKey(mulNode, RPR_MATERIAL_INPUT_COLOR0, scale, scale, scale, scale);
                }

                ~ScaleConversioNode() override {
                    if (mulNode) {
                        rprObjectDelete(mulNode);
                    }
                }

                rpr_status SetInput(rpr_material_node inputNode, LoaderContext* context) override {
                    return rprMaterialNodeSetInputNByKey(mulNode, RPR_MATERIAL_INPUT_COLOR1, inputNode);
                }

                rpr_material_node GetOutput() override {
                    return mulNode;
                }

                size_t MoveRprApiHandles(rpr_material_node* dst) override {
                    if (mulNode) {
                        if (dst) {
                            *dst = mulNode;
                            mulNode = nullptr;
                        }
                        return 1;
                    }

                    return 0;
                }
            };

            return std::make_unique<ScaleConversioNode>(scale, context);
        }
    };
    return std::make_unique<ScaleConverter>(scale);
}

//------------------------------------------------------------------------------
// Node implementation
//------------------------------------------------------------------------------

// TODO: add error handling
Node::Ptr Node::Create(mx::Node* mtlxNode, LoaderContext* context) {
    rpr_material_node rprNode = nullptr;
    Mtlx2Rpr::Node const* rprNodeMapping = nullptr;

    // Check for nodes with special handling first
    //
    if (mtlxNode->getCategory() == "surface") {
        auto surfaceDef = mtlxNode->getNodeDef();
        // The surface node has 3 inputs: bsdf, edf and opacity.
        // Right now we can not implement bsdf and edf blending and,
        // as a workaround, our surface node simply transfers bsdf node further along connections
        //
        struct SurfaceNode : public RprNode {
            SurfaceNode() : RprNode(nullptr, false) {}

            rpr_status SetInput(mx::TypedElement* downstreamElement, mx::Element* upstreamElement, rpr_material_node upstreamRprNode, LoaderContext* context) override {
                if (downstreamElement->getName() == "bsdf") {
                    rprNode = upstreamRprNode;
                    return RPR_SUCCESS;
                } else {
                    LOG(context, "Unsupported surface input: %s", downstreamElement->getName().c_str());
                    return RPR_ERROR_UNSUPPORTED;
                }
            }
        };

        return std::make_unique<SurfaceNode>();
    } else if (mtlxNode->getCategory() == "displacement") {
        return std::make_unique<DisplacementNode>(context);
    } else if (mtlxNode->getCategory() == "convert") {
        return std::make_unique<PassthroughNode>("in");
    } else if (mtlxNode->getCategory() == "texcoord") {
        rprMaterialSystemCreateNode(context->rprMatSys, RPR_MATERIAL_NODE_INPUT_LOOKUP, &rprNode);
        rprMaterialNodeSetInputUByKey(rprNode, RPR_MATERIAL_INPUT_VALUE, RPR_MATERIAL_NODE_LOOKUP_UV);
    } else if (mtlxNode->getCategory() == "normal") {
        rprMaterialSystemCreateNode(context->rprMatSys, RPR_MATERIAL_NODE_INPUT_LOOKUP, &rprNode);
        rprMaterialNodeSetInputUByKey(rprNode, RPR_MATERIAL_INPUT_VALUE, RPR_MATERIAL_NODE_LOOKUP_N);
    } else if (mtlxNode->getCategory() == "viewdirection") {
        rprMaterialSystemCreateNode(context->rprMatSys, RPR_MATERIAL_NODE_INPUT_LOOKUP, &rprNode);
        rprMaterialNodeSetInputUByKey(rprNode, RPR_MATERIAL_INPUT_VALUE, RPR_MATERIAL_NODE_LOOKUP_INVEC);
    } else if (mtlxNode->getCategory() == "sqrt") {
        rprMaterialSystemCreateNode(context->rprMatSys, RPR_MATERIAL_NODE_ARITHMETIC, &rprNode);
        rprMaterialNodeSetInputUByKey(rprNode, RPR_MATERIAL_INPUT_OP, RPR_MATERIAL_NODE_OP_POW);
        rprMaterialNodeSetInputFByKey(rprNode, RPR_MATERIAL_INPUT_COLOR1, 0.5f, 0.5f, 0.5f, 1.0f);
        static Mtlx2Rpr::Node s_sqrtMapping = {
            RPR_MATERIAL_NODE_ARITHMETIC, {
                {"in", RPR_MATERIAL_INPUT_COLOR0}
            }
        };
        rprNodeMapping = &s_sqrtMapping;
    } else if (mtlxNode->getCategory() == "image") {
        return std::make_unique<RprImageNode>(mtlxNode->getType(), context);
    } else if (mtlxNode->getCategory() == "rpr_uberv2") {
        return std::make_unique<RprUberNode>(context);
    } else if (mtlxNode->getCategory() == "swizzle") {
        // TODO: implement healthy man swizzle

        std::string channels;
        if (auto channelsParam = mtlxNode->getActiveParameter("channels")) {
            auto& valueString = channelsParam->getValueString();
            if (channelsParam->getType() == "string" &&
                !valueString.empty()) {
                channels = valueString;
            }
        }

        rpr_material_node_arithmetic_operation op;
        if (channels == "x") {
            op = RPR_MATERIAL_NODE_OP_SELECT_X;
        } else if (channels == "y") {
            op = RPR_MATERIAL_NODE_OP_SELECT_Y;
        } else {
            return nullptr;
        }

        rprMaterialSystemCreateNode(context->rprMatSys, RPR_MATERIAL_NODE_ARITHMETIC, &rprNode);
        rprMaterialNodeSetInputUByKey(rprNode, RPR_MATERIAL_INPUT_OP, op);

        static Mtlx2Rpr::Node s_swizzleMapping = {
            RPR_MATERIAL_NODE_ARITHMETIC, {
                {"in", RPR_MATERIAL_INPUT_COLOR0}
            }
        };
        rprNodeMapping = &s_swizzleMapping;

    // Then process standard nodes that can be mapped directly to RPR nodes
    //
    } else {
        // Some of the materialX standard nodes map to RPR differently depending on the node return type
        //
        if (mtlxNode->getCategory() == "mix" && (mtlxNode->getType() == "BSDF" || mtlxNode->getType() == "surfaceshader")) {
            // In RPR, mixing of two BSDFs can be done with RPR_MATERIAL_NODE_BLEND
            //
            static Mtlx2Rpr::Node bsdfMix = {
                RPR_MATERIAL_NODE_BLEND, {
                    {"fg", RPR_MATERIAL_INPUT_COLOR1},
                    {"bg", RPR_MATERIAL_INPUT_COLOR0},
                    {"mix", RPR_MATERIAL_INPUT_WEIGHT},
                }
            };
            rprNodeMapping = &bsdfMix;
        } else {
            auto rprNodeMappingIt = GetMtlx2Rpr().nodes.find(mtlxNode->getCategory());
            if (rprNodeMappingIt != GetMtlx2Rpr().nodes.end()) {
                rprNodeMapping = &rprNodeMappingIt->second;
            } else {
                // But direct mapping might not have been implemented yet or
                // this node might be of a custom definition
                //
                if (auto nodeDef = mtlxNode->getNodeDef()) {
                    if (auto nodeGraph = GetNodeGraphImpl(nodeDef.get())) {
                        try {
                            return std::make_unique<MtlxNodeGraphNode>(std::move(nodeGraph), context);
                        } catch (MtlxNodeGraphNode::NoOutputsError& e) {
                            // no-op
                        }
                    }
                }

                // TODO: code generation required

                LOG(context, "Unsupported node: %s (%s)", mtlxNode->getName().c_str(), mtlxNode->getCategory().c_str());
            }
        }
    }

    if (!rprNode && rprNodeMapping) {
        auto status = rprMaterialSystemCreateNode(context->rprMatSys, rprNodeMapping->id, &rprNode);
        if (status != RPR_SUCCESS) {
            LOG_ERROR(context, "failed to create %s (%s) node: %d", mtlxNode->getName().c_str(), mtlxNode->getCategory().c_str(), status);
            return nullptr;
        }

        // For arithmetic nodes, we also must not forget to set operation
        //
        if (rprNodeMapping->id == RPR_MATERIAL_NODE_ARITHMETIC) {
            auto it = GetMtlx2Rpr().arithmeticOps.find(mtlxNode->getCategory());
            if (it != GetMtlx2Rpr().arithmeticOps.end()) {
                rprMaterialNodeSetInputUByKey(rprNode, RPR_MATERIAL_INPUT_OP, it->second);
            } else {
                LOG_ERROR(context, "unknown arithmetic node: %s (%s)", mtlxNode->getName().c_str(), mtlxNode->getCategory().c_str());
            }
        }
    }

    if (!rprNode) {
        return nullptr;
    }
    rprObjectSetName(rprNode, mtlxNode->getName().c_str());

    if (rprNodeMapping) {
        return std::make_unique<RprMappedNode>(rprNode, rprNodeMapping);
    } else {
        return std::make_unique<RprNode>(rprNode, true);
    }
}

//------------------------------------------------------------------------------
// RprWrapNode implementation
//------------------------------------------------------------------------------

bool RprWrapNode::IsOutputTypeSupported(std::string const& outputType) {
    return outputType == "color3" || outputType == "color2" ||
           outputType == "vector3" || outputType == "vector2" ||
           outputType == "boolean" || outputType == "float";
}

RprWrapNode::RprWrapNode(LoaderContext* ctx)
    : RprNode(nullptr, true) {
    rprMaterialSystemCreateNode(ctx->rprMatSys, RPR_MATERIAL_NODE_PASSTHROUGH, &rprNode);
}

rpr_status RprWrapNode::SetInput(mx::TypedElement* downstreamElement, mx::Element* upstreamElement, rpr_material_node upstreamRprNode, LoaderContext* context) {
    return rprMaterialNodeSetInputNByKey(rprNode, RPR_MATERIAL_INPUT_COLOR, upstreamRprNode);
}

//------------------------------------------------------------------------------
// MtlxNodeGraphNode implementation
//------------------------------------------------------------------------------

MtlxNodeGraphNode::MtlxNodeGraphNode(mx::ConstGraphElementPtr const& mtlxGraph, LazyInit)
    : mtlxGraph(mtlxGraph) {

}

MtlxNodeGraphNode::MtlxNodeGraphNode(mx::ConstGraphElementPtr const& mtlxGraph, LoaderContext* context)
    : MtlxNodeGraphNode(mtlxGraph, mtlxGraph->getOutputs(), context) {

}

MtlxNodeGraphNode::MtlxNodeGraphNode(
    mx::ConstGraphElementPtr const& graph,
    std::vector<mx::OutputPtr> const& requiredOutputs,
    LoaderContext* context)
    : mtlxGraph(graph) {

    LOG(context, "NodeGraph: %s", mtlxGraph->getName().c_str());
    auto graphScope = context->EnterScope(LSGraph, mtlxGraph.get());

    bool hasAnyOutputNode = false;
    for (auto& output : requiredOutputs) {
        LOG(context, "Output: %s -> %s ", output->getName().c_str(), output->getNodeName().c_str());

        // An output of a node graph must have a `nodename` attribute
        //
        auto subNode = GetSubNode(output->getNodeName(), context);
        if (subNode) {
            hasAnyOutputNode = true;
        } else {
            LOG_ERROR(context, "Failed to create node %s in %s", output->getNodeName().c_str(), mtlxGraph->getName().c_str());
        }
    }

    if (!hasAnyOutputNode) {
        throw NoOutputsError();
    }
}

Node* MtlxNodeGraphNode::GetSubNode(std::string const& nodename, LoaderContext* context) {
    auto subNodeIt = subNodes.find(nodename);
    if (subNodeIt != subNodes.end()) {
        return subNodeIt->second.get();
    }

    auto mtlxNode = mtlxGraph->getNode(nodename);
    if (!mtlxNode) {
        LOG_ERROR(context, "No node with such name: %s", nodename.c_str());
        return nullptr;
    }

    return _CreateSubNode(mtlxNode, context);
}

Node* MtlxNodeGraphNode::_CreateSubNode(
    mx::NodePtr const& mtlxNode,
    LoaderContext* context) {
    if (!mtlxNode) {
        return nullptr;
    }

    auto subNodeIt = subNodes.find(mtlxNode->getName());
    if (subNodeIt != subNodes.end()) {
        return subNodeIt->second.get();
    }

    LOG(context, "Node: %s (%s)", mtlxNode->getName().c_str(), mtlxNode->getCategory().c_str());
    auto nodeScope = context->EnterScope(LSNode, mtlxNode.get());

    auto nodeHandle = Node::Create(mtlxNode.get(), context);
    if (!nodeHandle) {
        return nullptr;
    }

    auto node = nodeHandle.get();
    subNodes.emplace(mtlxNode->getName(), std::move(nodeHandle));

    auto nodeDef = mtlxNode->getNodeDef();
    if (!nodeDef) {
        LOG_ERROR(context, "Failed to get mtlxNode definition: %s", mtlxNode->asString().c_str());
        return node;
    }

    // Iterate over nodeDef's inputs/parameters and apply them
    //
    for (auto& nodeDefChild : nodeDef->getChildren()) {
        if (nodeDefChild->getCategory() == "output") {
            continue;
        }

        // ValueElement is a common base type
        //
        auto inputElement = nodeDefChild->asA<mx::ValueElement>();
        if (!inputElement) {
            continue;
        }

        LOG(context, "%s %s", inputElement->getCategory().c_str(), inputElement->getName().c_str());
        auto inputScope = context->EnterScope(LSInput, inputElement.get());

        // An element that provides a value for the current input
        //
        auto valueElement = inputElement;

        // mtlxNode may override one of the inputs/parameters defined in nodeDef.
        //
        if (auto mtlxNodeChild = mtlxNode->getChild(valueElement->getName())) {
            if (auto mtlxNodeInputElement = mtlxNodeChild->asA<mx::ValueElement>()) {
                auto& interfaceName = mtlxNodeInputElement->getInterfaceName();
                if (!interfaceName.empty()) {
                    // If this input has an interface name then the instantiator of this node might override this value later,
                    // this input will be referenced by the interface name, so we map this name to a particular input socket
                    //
                    _interfaceSockets[interfaceName].push_back({mtlxNode, valueElement});
                    //
                    // For now, get value from a nodeDef of the current nodeGraph
                    //
                    if (auto nodeGraph = mtlxGraph->asA<mx::NodeGraph>()) {
                        if (auto nodeGraphDef = nodeGraph->getNodeDef()) {
                            if (auto nodeGraphDefInput = nodeGraphDef->getInput(interfaceName)) {
                                valueElement = nodeGraphDefInput;
                            }
                        }
                    }
                } else {
                    valueElement = std::move(mtlxNodeInputElement);
                }
            }
        }

        rpr_status status = RPR_SUCCESS;

        // A input element may be of different types: Input or Parameter (removed in 1.38).
        //
        // If an input element is of Input type, it can specify its value through:
        //
        if (auto input = valueElement->asA<mx::Input>()) {
            // `nodename` attribute - an input connection to the node of the current graph
            //
            auto& nodeName = input->getNodeName();
            if (!nodeName.empty()) {
                LOG(context, "nodename: %s", nodeName.c_str());

                auto mtlxUpstreamNode = mtlxGraph->getNode(nodeName);
                if (!mtlxUpstreamNode) {
                    LOG_ERROR(context, "Node \"%s\" cannot be found in \"%s\"", nodeName.c_str(), mtlxGraph->getName().c_str());
                    continue;
                }

                auto mtlxUpstreamNodeOutput = GetOutput(mtlxUpstreamNode.get(), input.get(), context);
                if (!mtlxUpstreamNodeOutput && mtlxUpstreamNode->getType() == mx::MULTI_OUTPUT_TYPE_STRING) {
                    continue;
                }

                Node* upstreamNode = _CreateSubNode(mtlxUpstreamNode, context);
                if (upstreamNode) {
                    auto& outputName = mtlxUpstreamNodeOutput ? mtlxUpstreamNodeOutput->getName() : mx::EMPTY_STRING;
                    auto status = upstreamNode->Connect(outputName, node, inputElement.get(), context);

                    if (status == RPR_SUCCESS) {
                        LOG(context, "Connected %s to %s", mtlxUpstreamNode->getName().c_str(), mtlxNode->getName().c_str());
                    }
                } else {
                    LOG(context, "Failed to connect %s to %s", mtlxUpstreamNode->getName().c_str(), mtlxNode->getName().c_str());
                    status = RPR_ERROR_INVALID_OBJECT;
                }

                if (status == RPR_SUCCESS) {
                    continue;
                }
            }

            // `output`[&`nodegraph`] attribute[s] - an input connection to a freestanding output[/nodegraph]
            //
            if (context->ConnectToGlobalOutput(input.get(), node)) {
                continue;
            }

            // `defaultgeomprop` attribute - an intrinsic geometric property
            //
            auto& defaultGeomProp = input->getDefaultGeomPropString();
            if (!defaultGeomProp.empty()) {
                if (auto geomPropDef = context->mtlxDocument->getGeomPropDef(defaultGeomProp)) {
                    if (auto geomNode = context->GetGeomNode(geomPropDef.get())) {
                        status = geomNode->Connect(mx::EMPTY_STRING, node, inputElement.get(), context);
                    }
                } else {
                    LOG_ERROR(context, "Unkown defaultgeomprop: %s", defaultGeomProp.c_str());
                }

                continue;
            }
        }

        // `value` attribute - a uniform value
        //
        auto& valueStr = valueElement->getValueString();
        if (!valueStr.empty()) {
            LOG(context, "%s", valueStr.c_str());

            status = node->SetInput(inputElement.get(), valueElement.get(), context);
            if (status == RPR_SUCCESS) {
                continue;
            }
        }
    }

    return node;
}

rpr_status MtlxNodeGraphNode::Connect(std::string const& upstreamOutput, Node* downstreamNode, mx::TypedElement* downstreamElement, LoaderContext* context) {
    mx::OutputPtr output;
    if (!upstreamOutput.empty()) {
        output = mtlxGraph->getOutput(upstreamOutput);
    } else {
        output = GetFirst<mx::Output>(mtlxGraph.get());
    }

    if (output && !output->getNodeName().empty()) {
        auto nodeIt = subNodes.find(output->getNodeName());
        if (nodeIt != subNodes.end()) {
            return nodeIt->second->Connect(output->getOutputString(), downstreamNode, downstreamElement, context);
        }
    }

    return RPR_ERROR_INVALID_PARAMETER;
}

MtlxNodeGraphNode::InterfaceSocketsQuery MtlxNodeGraphNode::_GetInterfaceSockets(mx::TypedElement* inputElement) {
    if (auto valueElement = inputElement->asA<mx::ValueElement>()) {
        auto& interfaceName = valueElement->getInterfaceName();
        if (!interfaceName.empty()) {
            if (auto query = _GetInterfaceSockets(interfaceName)) {
                return query;
            }
        }
    }

    return _GetInterfaceSockets(inputElement->getName());
}

MtlxNodeGraphNode::InterfaceSocketsQuery MtlxNodeGraphNode::_GetInterfaceSockets(std::string const& interfaceName) {
    auto it = _interfaceSockets.find(interfaceName);
    if (it == _interfaceSockets.end()) {
        return {};
    }
    return {this, &it->second};
}

rpr_status MtlxNodeGraphNode::SetInput(mx::TypedElement* downstreamElement, mx::Element* upstreamElement, rpr_material_node upstreamRprNode, LoaderContext* context) {
    if (auto sockets = _GetInterfaceSockets(downstreamElement)) {
        rpr_status status = RPR_SUCCESS;
        sockets.ForEach(context,
            [&](Node* socketNode, mx::TypedElement* socketDownstreamElement) {
                auto setInputStatus = socketNode->SetInput(socketDownstreamElement, upstreamElement, upstreamRprNode, context);
                if (setInputStatus != RPR_SUCCESS) {
                    status = setInputStatus;
                }
            }
        );
        return status;
    }

    LOG_ERROR(context, "failed to set %s input for %s: no such interface socket", downstreamElement->getName().c_str(), mtlxGraph->getName().c_str());
    return RPR_ERROR_INVALID_PARAMETER;
}

rpr_status MtlxNodeGraphNode::SetInput(mx::TypedElement* downstreamElement, mx::ValueElement* upstreamValueElement, LoaderContext* context) {
    if (auto sockets = _GetInterfaceSockets(downstreamElement)) {
        rpr_status status = RPR_SUCCESS;
        sockets.ForEach(context,
            [&](Node* socketNode, mx::TypedElement* socketDownstreamElement) {
                auto setInputStatus = socketNode->SetInput(socketDownstreamElement, upstreamValueElement, context);
                if (setInputStatus != RPR_SUCCESS) {
                    status = setInputStatus;
                }
            }
        );
        return status;
    }

    LOG_ERROR(context, "failed to set %s input for %s: no such interface socket", downstreamElement->getName().c_str(), mtlxGraph->getName().c_str());
    return RPR_ERROR_INVALID_PARAMETER;
}

//------------------------------------------------------------------------------
// RprNode implementation
//------------------------------------------------------------------------------

RprNode::RprNode(rpr_material_node node, bool retainNode)
    : isOwningRprNode(retainNode), rprNode(node) {

}

RprNode::~RprNode() {
    if (rprNode && isOwningRprNode) {
        rprObjectDelete(rprNode);
    }
}

rpr_status RprNode::Connect(std::string const& upstreamOutput, Node* downstreamNode, mx::TypedElement* downstreamElement, LoaderContext* context) {
    // Ignoring upstreamOutput because rprNode is the only possible output
    //
    return downstreamNode->SetInput(downstreamElement, nullptr, rprNode, context);
}

rpr_status RprNode::SetInput(mx::TypedElement* downstreamElement, mx::Element* upstreamElement, rpr_material_node upstreamRprNode, LoaderContext* context) {
    return RPR_ERROR_UNSUPPORTED;
}

rpr_status RprNode::SetInput(mx::TypedElement* downstreamElement, mx::ValueElement* upstreamValueElement, LoaderContext* context) {
    return RPR_ERROR_UNSUPPORTED;
}

bool GetInputF(mx::TypedElement* downstreamElement, mx::ValueElement* upstreamElement, std::string const& valueString, std::string const& valueType, LoaderContext* context, float dst[4]) {
    if (valueType == "float") {
        auto value = mx::fromValueString<float>(valueString);
        dst[0] = dst[1] = dst[2] = value;
        dst[3] = 0.0f;
    } else if (
        valueType == "vector2" ||
        valueType == "color2") {
        auto value = mx::fromValueString<mx::Color2>(valueString);
        dst[0] = value[0];
        dst[1] = value[1];
        dst[2] = dst[3] = 0.0f;
    } else if (
        valueType == "vector3" ||
        valueType == "color3") {
        auto value = mx::fromValueString<mx::Color3>(valueString);
        dst[0] = value[0];
        dst[1] = value[1];
        dst[2] = value[2];
        dst[3] = 0.0f;
    } else if (
        valueType == "vector4" ||
        valueType == "color4") {
        auto value = mx::fromValueString<mx::Color4>(valueString);
        dst[0] = value[0];
        dst[1] = value[1];
        dst[2] = value[2];
        dst[3] = value[3];
    } else {
        return false;
    }

    if (StringStartsWith(valueType, "color")) {
        auto& colorspace = upstreamElement ? upstreamElement->getActiveColorSpace() : downstreamElement->getActiveColorSpace();
        if (auto colorspaceConverter = context->GetColorSpaceConverter(colorspace)) {
            colorspaceConverter->Convert(dst, context);
            return true;
        }
    } else if (StringStartsWith(valueType, "vector") || valueType == "float") {
        if (auto unitConverter = context->GetUnitConverter(upstreamElement)) {
            unitConverter->Convert(dst, context);
            return true;
        }
    }

    return true;
}

rpr_status RprNode::SetInput(mx::TypedElement* downstreamElement, rpr_material_node_input downstreamRprId, mx::ValueElement* upstreamValueElement, LoaderContext* context) {
    auto& valueString = upstreamValueElement->getValueString();
    auto& valueType = upstreamValueElement->getType();

    try {
        float color[4];
        if (GetInputF(downstreamElement, upstreamValueElement, valueString, valueType, context, color)) {
            return rprMaterialNodeSetInputFByKey(rprNode, downstreamRprId, color[0], color[1], color[2], color[3]);
        } else if (
            valueType == "boolean") {
            auto value = static_cast<float>(mx::fromValueString<bool>(valueString));
            return rprMaterialNodeSetInputFByKey(rprNode, downstreamRprId, value, value, value, 0.0f);
        } else if (
            valueType == "integer") {
            auto value = static_cast<float>(mx::fromValueString<int>(valueString));
            return rprMaterialNodeSetInputFByKey(rprNode, downstreamRprId, value, value, value, 0.0f);
        } else {
            LOG_WARNING(context, "failed to parse %s value: unsupported type - %s", valueString.c_str(), valueType.c_str());
        }
    } catch (mx::ExceptionTypeError& e) {
        LOG_ERROR(context, "failed to parse %s value: %s", valueString.c_str(), e.what());
    }

    return RPR_ERROR_INVALID_PARAMETER;
}

size_t RprNode::MoveRprApiHandles(rpr_material_node* dst) {
    size_t i = 0;

    // Move only unique nodes
    //
    if (rprNode && isOwningRprNode) {
        if (dst) {
            dst[i] = rprNode;
            rprNode = nullptr;
        }
        i++;
    }

    for (auto& entry : conversionNodes) {
        i += entry.second->MoveRprApiHandles(dst ? &dst[i] : nullptr);
    }
    return i;
}

RprMappedNode::RprMappedNode(rpr_material_node node, Mtlx2Rpr::Node const* nodeMapping)
    : RprNode(node, true), rprNodeMapping(nodeMapping) {

}

rpr_status RprMappedNode::SetInput(mx::TypedElement* downstreamElement, mx::Element* upstreamElement, rpr_material_node upstreamRprNode, LoaderContext* context) {
    auto inputIt = rprNodeMapping->inputs.find(downstreamElement->getName());
    if (inputIt == rprNodeMapping->inputs.end()) {
        LOG_ERROR(context, "unknown input: %s", downstreamElement->getName().c_str());
        return RPR_ERROR_INVALID_PARAMETER;
    }

    std::unique_ptr<BaseConverterNode> inputConversionNode;

    auto& type = downstreamElement->getType();
    if (StringStartsWith(type, "color")) {
        auto& colorspace = upstreamElement ? upstreamElement->getActiveColorSpace() : downstreamElement->getActiveColorSpace();
        if (auto colorspaceConverter = context->GetColorSpaceConverter(colorspace)) {
            inputConversionNode = colorspaceConverter->GetConversionNode(context);
        }
    } else if (StringStartsWith(type, "vector") || type == "float") {
        if (auto unitConverter = context->GetUnitConverter(upstreamElement)) {
            inputConversionNode = unitConverter->GetConversionNode(context);
        }
    }

    if (inputConversionNode) {
        inputConversionNode->SetInput(upstreamRprNode, context);
        rprMaterialNodeSetInputNByKey(rprNode, inputIt->second, inputConversionNode->GetOutput());
        conversionNodes[downstreamElement->getName()] = std::move(inputConversionNode);
        return RPR_SUCCESS;
    }

    return rprMaterialNodeSetInputNByKey(rprNode, inputIt->second, upstreamRprNode);
}

rpr_status RprMappedNode::SetInput(mx::TypedElement* downstreamElement, mx::ValueElement* valueElement, LoaderContext* context) {
    auto inputIt = rprNodeMapping->inputs.find(downstreamElement->getName());
    if (inputIt == rprNodeMapping->inputs.end()) {
        LOG_ERROR(context, "unknown input: %s", downstreamElement->getName().c_str());
        return RPR_ERROR_INVALID_PARAMETER;
    }

    return RprNode::SetInput(downstreamElement, inputIt->second, valueElement, context);
}

RprImageNode::RprImageNode(std::string const& type, LoaderContext* context)
    : RprMappedNode(
        [context]() {
            rpr_material_node node = nullptr;
            rprMaterialSystemCreateNode(context->rprMatSys, RPR_MATERIAL_NODE_IMAGE_TEXTURE, &node);
            return node;
        }(),
        []() {
            static Mtlx2Rpr::Node s_imageMapping = {
                RPR_MATERIAL_NODE_IMAGE_TEXTURE, {
                    {"texcoord", RPR_MATERIAL_INPUT_UV}
                }
            };
            return &s_imageMapping;
        }()
    )
    , type(type) {

}

rpr_status RprImageNode::Connect(std::string const& upstreamOutput, Node* downstreamNode, mx::TypedElement* downstreamElement, LoaderContext* context) {
    if (!fileValueElement) {
        return RPR_ERROR_INVALID_OBJECT;
    }

    if (isFileDirty) {
        isFileDirty = false;

        resolvedFilepath = context->ResolveFile(fileValueElement->getActiveFilePrefix() + fileValueElement->getValueString());

        std::unique_ptr<LoaderContext::ValueConverter> retainedUnitConverter;
        LoaderContext::ValueConverter* converter = nullptr;
        std::string conversionNodeId;

        auto& colorspace = fileValueElement->getActiveColorSpace();
        if (auto colorspaceConverter = context->GetColorSpaceConverter(colorspace)) {
            conversionNodeId = colorspace;
            converter = colorspaceConverter;
        } else if (auto unitConverter = context->GetUnitConverter(fileValueElement)) {
            conversionNodeId = fileValueElement->getUnit() + fileValueElement->getUnitType();
            retainedUnitConverter = std::move(unitConverter);
            converter = retainedUnitConverter.get();
        }

        disableRprImageColorspace = !colorspace.empty();

        if (converter) {
            auto conversionNodeIt = conversionNodes.find(conversionNodeId);
            if (conversionNodeIt == conversionNodes.end()) {
                if (auto conversionNode = converter->GetConversionNode(context)) {
                    // Release outdated conversion nodes
                    //
                    conversionNodes.clear();

                    conversionNode->SetInput(rprNode, context);
                    conversionNodeIt = conversionNodes.emplace(conversionNodeId, std::move(conversionNode)).first;
                }
            }
        }
    }

    if (!conversionNodes.empty()) {
        if (auto convertedOutput = conversionNodes.begin()->second->GetOutput()) {
            return downstreamNode->SetInput(downstreamElement, nullptr, convertedOutput, context);
        }
    }

    return downstreamNode->SetInput(downstreamElement, nullptr, rprNode, context);
}


rpr_status RprImageNode::SetInput(mx::TypedElement* downstreamElement, mx::ValueElement* upstreamValueElement, LoaderContext* context) {
    auto& value = upstreamValueElement->getValueString();
    auto& valueType = upstreamValueElement->getType();

    rpr_status status = RPR_SUCCESS;
    if (valueType == "string") {
        if (downstreamElement->getName() == "layer") {
            layer = value;
        } else if (downstreamElement->getName() == "uaddressmode") {
            uaddressmode = value;
        } else if (downstreamElement->getName() == "vaddressmode") {
            vaddressmode = value;
        } else if (downstreamElement->getName() == "filtertype" ||
                   downstreamElement->getName() == "framerange" ||
                   downstreamElement->getName() == "frameendaction") {
            // unprocessed for now
        } else {
            status = RPR_ERROR_INVALID_PARAMETER;
        }
    } else if (valueType == "filename") {
        if (downstreamElement->getName() == "file") {
            if (fileValueElement != upstreamValueElement) {
                fileValueElement = upstreamValueElement;
                isFileDirty = true;
            }
        } else {
            status = RPR_ERROR_INVALID_PARAMETER;
        }
    } else if (downstreamElement->getName() == "frameoffset" && valueType == "integer") {
        // unprocessed for now
    } else if (downstreamElement->getName() == "default") {
        defaultValue = mx::Value::createValueFromStrings(value, valueType);
    } else {
        status = RprMappedNode::SetInput(downstreamElement, upstreamValueElement, context);
    }

    if (status != RPR_SUCCESS) {
        LOG_ERROR(context, "Invalid input for image node %s (%s %s): unknown input or invalid type",
            downstreamElement->getName().c_str(), value.c_str(), valueType.c_str());
    }
    return status;
}

RprUberNode::RprUberNode(LoaderContext* context)
    : RprMappedNode(
        [context]() {
            rpr_material_node node = nullptr;
            rprMaterialSystemCreateNode(context->rprMatSys, RPR_MATERIAL_NODE_UBERV2, &node);
            return node;
        }(),
            []() {
            static Mtlx2Rpr::Node s_imageMapping = {
                RPR_MATERIAL_NODE_UBERV2, {
                    {"uber_diffuse_color", RPR_MATERIAL_INPUT_UBER_DIFFUSE_COLOR},
                    {"uber_diffuse_weight", RPR_MATERIAL_INPUT_UBER_DIFFUSE_WEIGHT},
                    {"uber_diffuse_roughness", RPR_MATERIAL_INPUT_UBER_DIFFUSE_ROUGHNESS},
                    {"uber_diffuse_normal", RPR_MATERIAL_INPUT_UBER_DIFFUSE_NORMAL},
                    {"uber_reflection_color", RPR_MATERIAL_INPUT_UBER_REFLECTION_COLOR},
                    {"uber_reflection_weight", RPR_MATERIAL_INPUT_UBER_REFLECTION_WEIGHT},
                    {"uber_reflection_roughness", RPR_MATERIAL_INPUT_UBER_REFLECTION_ROUGHNESS},
                    {"uber_reflection_anisotropy", RPR_MATERIAL_INPUT_UBER_REFLECTION_ANISOTROPY},
                    {"uber_reflection_anisotropy_rotation", RPR_MATERIAL_INPUT_UBER_REFLECTION_ANISOTROPY_ROTATION},
                    {"uber_reflection_ior", RPR_MATERIAL_INPUT_UBER_REFLECTION_IOR},
                    {"uber_reflection_metalness", RPR_MATERIAL_INPUT_UBER_REFLECTION_METALNESS},
                    {"uber_reflection_normal", RPR_MATERIAL_INPUT_UBER_REFLECTION_NORMAL},
                    {"uber_refraction_color", RPR_MATERIAL_INPUT_UBER_REFRACTION_COLOR},
                    {"uber_refraction_weight", RPR_MATERIAL_INPUT_UBER_REFRACTION_WEIGHT},
                    {"uber_refraction_roughness", RPR_MATERIAL_INPUT_UBER_REFRACTION_ROUGHNESS},
                    {"uber_refraction_ior", RPR_MATERIAL_INPUT_UBER_REFRACTION_IOR},
                    {"uber_refraction_normal", RPR_MATERIAL_INPUT_UBER_REFRACTION_NORMAL},
                    {"uber_refraction_thin_surface", RPR_MATERIAL_INPUT_UBER_REFRACTION_THIN_SURFACE},
                    {"uber_refraction_absorption_color", RPR_MATERIAL_INPUT_UBER_REFRACTION_ABSORPTION_COLOR},
                    {"uber_refraction_absorption_distance", RPR_MATERIAL_INPUT_UBER_REFRACTION_ABSORPTION_DISTANCE},
                    {"uber_refraction_caustics", RPR_MATERIAL_INPUT_UBER_REFRACTION_CAUSTICS},
                    {"uber_coating_color", RPR_MATERIAL_INPUT_UBER_COATING_COLOR},
                    {"uber_coating_weight", RPR_MATERIAL_INPUT_UBER_COATING_WEIGHT},
                    {"uber_coating_roughness", RPR_MATERIAL_INPUT_UBER_COATING_ROUGHNESS},
                    {"uber_coating_ior", RPR_MATERIAL_INPUT_UBER_COATING_IOR},
                    {"uber_coating_metalness", RPR_MATERIAL_INPUT_UBER_COATING_METALNESS},
                    {"uber_coating_normal", RPR_MATERIAL_INPUT_UBER_COATING_NORMAL},
                    {"uber_coating_transmission_color", RPR_MATERIAL_INPUT_UBER_COATING_TRANSMISSION_COLOR},
                    {"uber_coating_thickness", RPR_MATERIAL_INPUT_UBER_COATING_THICKNESS},
                    {"uber_sheen", RPR_MATERIAL_INPUT_UBER_SHEEN},
                    {"uber_sheen_tint", RPR_MATERIAL_INPUT_UBER_SHEEN_TINT},
                    {"uber_sheen_weight", RPR_MATERIAL_INPUT_UBER_SHEEN_WEIGHT},
                    {"uber_emission_color", RPR_MATERIAL_INPUT_UBER_EMISSION_COLOR},
                    {"uber_emission_weight", RPR_MATERIAL_INPUT_UBER_EMISSION_WEIGHT},
                    {"uber_transparency", RPR_MATERIAL_INPUT_UBER_TRANSPARENCY},
                    {"uber_sss_scatter_color", RPR_MATERIAL_INPUT_UBER_SSS_SCATTER_COLOR},
                    {"uber_sss_scatter_distance", RPR_MATERIAL_INPUT_UBER_SSS_SCATTER_DISTANCE},
                    {"uber_sss_scatter_direction", RPR_MATERIAL_INPUT_UBER_SSS_SCATTER_DIRECTION},
                    {"uber_sss_weight", RPR_MATERIAL_INPUT_UBER_SSS_WEIGHT},
                    {"uber_sss_multiscatter", RPR_MATERIAL_INPUT_UBER_SSS_MULTISCATTER},
                    {"uber_backscatter_weight", RPR_MATERIAL_INPUT_UBER_BACKSCATTER_WEIGHT},
                    {"uber_backscatter_color", RPR_MATERIAL_INPUT_UBER_BACKSCATTER_COLOR},
                    {"uber_fresnel_schlick_approximation", RPR_MATERIAL_INPUT_UBER_FRESNEL_SCHLICK_APPROXIMATION},
                    // handled in SetInput
                    //{"uber_reflection_mode", RPR_MATERIAL_INPUT_UBER_REFLECTION_MODE},
                    //{"uber_coating_mode", RPR_MATERIAL_INPUT_UBER_COATING_MODE},
                    //{"uber_emission_mode", RPR_MATERIAL_INPUT_UBER_EMISSION_MODE},
                }
            };
            return &s_imageMapping;
        }()
    ) {

}

rpr_status RprUberNode::SetInput(mx::TypedElement* downstreamElement, mx::ValueElement* upstreamValueElement, LoaderContext* context) {
    auto& value = upstreamValueElement->getValueString();
    auto& valueType = upstreamValueElement->getType();

    rpr_status status = RPR_SUCCESS;
    if (valueType == "string") {
        if (downstreamElement->getName() == "uber_reflection_mode" ||
            downstreamElement->getName() == "uber_coating_mode") {
            rpr_ubermaterial_ior_mode mode;
            if (value == "Metalness") {
                mode = RPR_UBER_MATERIAL_IOR_MODE_METALNESS;
            } else {
                mode = RPR_UBER_MATERIAL_IOR_MODE_PBR;
            }
            rpr_material_node_input inputKey;
            if (downstreamElement->getName() == "uber_reflection_mode") {
                inputKey = RPR_MATERIAL_INPUT_UBER_REFLECTION_MODE;
            } else {
                inputKey = RPR_MATERIAL_INPUT_UBER_COATING_MODE;
            }
            status = rprMaterialNodeSetInputUByKey(rprNode, inputKey, mode);
        } else if (downstreamElement->getName() == "uber_emission_mode") {
            rpr_ubermaterial_emission_mode mode;
            if (value == "Doublesided") {
                mode = RPR_UBER_MATERIAL_EMISSION_MODE_DOUBLESIDED;
            } else {
                mode = RPR_UBER_MATERIAL_EMISSION_MODE_SINGLESIDED;
            }
            status = rprMaterialNodeSetInputUByKey(rprNode, RPR_MATERIAL_INPUT_UBER_EMISSION_MODE, mode);
        } else {
            status = RPR_ERROR_INVALID_PARAMETER;
        }
    } else {
        status = RprMappedNode::SetInput(downstreamElement, upstreamValueElement, context);
    }

    if (status != RPR_SUCCESS &&
        status != RPR_ERROR_UNSUPPORTED) {
        LOG_ERROR(context, "Invalid input for uber node %s (%s %s): unknown input or invalid type",
            downstreamElement->getName().c_str(), value.c_str(), valueType.c_str());
    }
    return status;
}


//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------

bool IsSupportedTarget(std::string const& target) {
    // We currently support only universal nodes
    return target.empty();
}

RPRMtlxLoader::OutputType ToOutputType(std::string const& type) {
    if (type == "surfaceshader" ||
        RprWrapNode::IsOutputTypeSupported(type)) {
        return RPRMtlxLoader::kOutputSurface;
    } else if (type == "displacementshader") {
        return RPRMtlxLoader::kOutputDisplacement;
    } else {
        return RPRMtlxLoader::kOutputNone;
    }
}

bool IsRenderableType(std::string const& mtlxTypeString) {
    auto mtlxType = ToOutputType(mtlxTypeString);
    return mtlxType != RPRMtlxLoader::kOutputNone;
}

// F must be a function with bool(mx::OutputPtr const&, mx::ShaderRefPtr const& shaderRef) signature.
// Returned boolean controls whether ForEachOutput should continue traversing
template <typename F>
void ForEachOutput(mx::ElementPtr const& element, F&& func) {
    if (auto material = element->asA<mx::Material>()) {
        for (auto& shaderRef : material->getShaderRefs()) {
            if (auto nodeDef = shaderRef->getNodeDef()) {
                if (IsSupportedTarget(nodeDef->getTarget())) {
                    if (auto nodeGraph = GetNodeGraphImpl(nodeDef.get())) {
                        for (auto& child : nodeGraph->getChildren()) {
                            if (auto output = child->asA<mx::Output>()) {
                                if (!func(output, shaderRef)) {
                                    return;
                                }
                            }
                        }
                    }
                }
            }
        }
    } else if (auto nodeGraph = element->asA<mx::NodeGraph>()) {
        for (auto& child : nodeGraph->getChildren()) {
            if (auto output = child->asA<mx::Output>()) {
                if (!func(output, nullptr)) {
                    return;
                }
            }
        }
    } else if (auto output = element->asA<mx::Output>()) {
        func(output, nullptr);
    } else if (auto node = element->asA<mx::Node>()) {
        auto processShaderNode = [&func](mx::NodePtr const& shaderNode) {
            auto referenceOutput = std::make_shared<mx::Output>(shaderNode->getParent(), "out");
            referenceOutput->setNodeName(shaderNode->getName());
            referenceOutput->setType(shaderNode->getType());
            return func(referenceOutput, nullptr);
        };

        auto& nodeType = node->getType();
        if (nodeType == "material") {
            for (auto& shaderType : {"surfaceshader", "displacementshader"}) {
                if (auto shader = node->getInput(shaderType)) {
                    if (mx::ElementPtr shaderNodeElement = element->getParent()->getChild(shader->getNodeName())) {
                        if (auto shaderNode = shaderNodeElement->asA<mx::Node>()) {
                            if (!processShaderNode(shaderNode)) {
                                return;
                            }
                        }
                    }
                }
            }
        } else if (
            nodeType == "surfaceshader" ||
            nodeType == "displacementshader") {
            if (!processShaderNode(node)) {
                return;
            }
        }
    }
}

bool IsMaterialHasRenderableOutputs(mx::MaterialPtr const& material) {
    bool hasRenderableOutput = false;
    ForEachOutput(material,
        [&hasRenderableOutput](mx::OutputPtr const& output, mx::ShaderRefPtr const&) {
        if (IsRenderableType(output->getType())) {
            hasRenderableOutput = true;
        }
        // Keep iterating untill we find a renderable output
        return !hasRenderableOutput;
    });
    return hasRenderableOutput;
}

// Alternative to mx::Element::getChildrenOfType. This function avoids using std::vector.
// F must be a function with void(std::shared_ptr<T>) signature
template <typename T, typename F, typename S>
void ForEachChildrenOfType(mx::Element const* element, S&& shouldStop, F&& func) {
    if (shouldStop()) { return; }

    for (auto& child : element->getChildren()) {
        if (auto typedChild = child->asA<T>()) {
            func(typedChild);

            if (shouldStop()) { return; }
        }
    }
}

// F must be a function with void(mx::ElementPtr const&) signature.
template <typename F, typename S>
void ForEachRenderableElement(mx::Document const* mtlxDocument, S&& shouldStop, F&& func) {
    // We are not using looks for what they are designed (the assignment of material to geometry)
    // because RPRMtlxLoader does not deal with geometry.
    // Though, we use it to get hints from mtlx file about available materials
    ForEachChildrenOfType<mx::Look>(mtlxDocument, shouldStop, [&](auto const& look) {
        ForEachChildrenOfType<mx::MaterialAssign>(look.get(), shouldStop,
            [&](auto const& materialAssign) {
                if (auto material = mtlxDocument->getChild(materialAssign->getMaterial())) {
                    func(material);
                }
            }
        );
    });

    ForEachChildrenOfType<mx::Material>(mtlxDocument, shouldStop,
        [&func](auto const& material) {
            if (IsMaterialHasRenderableOutputs(material)) {
                func(material);
            }
        }
    );

    auto mtlxVersion = mtlxDocument->getVersionIntegers();
    if (mtlxVersion.first >= 1 && mtlxVersion.second >= 38) {
        // Iterate over all global shader nodes
        //
        ForEachChildrenOfType<mx::Node>(mtlxDocument, shouldStop,
            [&func, &mtlxDocument](auto const& node) {
                if (!node->hasSourceUri()) {
                    if (auto typeDef = mtlxDocument->getTypeDef(node->getType())) {
                        if (typeDef->getSemantic() == mx::SHADER_SEMANTIC) {
                            func(node);
                        }
                    }
                }
            }
        );
    }

    ForEachChildrenOfType<mx::NodeGraph>(mtlxDocument, shouldStop,
        [&func](auto const& nodeGraph) {
            // Skip anything from an include file including libraries.
            // Skip any nodegraph which is a definition
            //
            if (nodeGraph->hasSourceUri() ||
                nodeGraph->hasAttribute(mx::InterfaceElement::NODE_DEF_ATTRIBUTE)) {
                return;
            }

            bool stop = false;
            ForEachChildrenOfType<mx::Output>(nodeGraph.get(),
                [&stop]() { return stop; },
                [&](auto const& output) {
                    if (ToOutputType(output->getType()) != RPRMtlxLoader::kOutputNone) {
                        func(nodeGraph);
                        stop = true;
                    }
                }
            );
        }
    );

    // Iterate over all global outputs
    //
    ForEachChildrenOfType<mx::Output>(mtlxDocument, shouldStop,
        [&func](auto const& output) {
            if (!output->hasSourceUri()) {
                if (ToOutputType(output->getType()) != RPRMtlxLoader::kOutputNone) {
                    func(output);
                }
            }
        }
    );
}

template <typename F>
void TraverseNode(Node* node, F&& cb) {
    cb(node);
    if (auto mtlxGraphNode = node->AsA<MtlxNodeGraphNode>()) {
        for (auto& entry : mtlxGraphNode->subNodes) {
            TraverseNode(entry.second.get(), cb);
        }
    }
}

struct GraphNodesKey {
    mx::GraphElement const* nodeGraph;
    mx::ShaderRef const* shaderRef;

    struct Hash {
        size_t operator()(GraphNodesKey const& key) const {
            return GetHash(key.nodeGraph) ^ GetHash(key.shaderRef);
        }
    };

    bool operator==(GraphNodesKey const& rhs) const {
        return nodeGraph == rhs.nodeGraph &&
               shaderRef == rhs.shaderRef;
    }
};
struct GraphNodesValue {
    std::vector<RPRMtlxLoader::OutputType> outputTypes;
    MtlxNodeGraphNode::Ptr node;
    RprWrapNode::Ptr wrapNode;
};
using GraphNodes = std::unordered_map<GraphNodesKey, GraphNodesValue, GraphNodesKey::Hash>;

template <typename F>
void TraverseGraphNodes(GraphNodes const& graphNodes, LoaderContext* ctx, F&& cb) {
    for (auto& entry : graphNodes) {
        if (entry.second.node) {
            TraverseNode(entry.second.node.get(), cb);
        }
        if (entry.second.wrapNode) {
            TraverseNode(entry.second.wrapNode.get(), cb);
        }
    }
    if (ctx->globalNodeGraph) {
        TraverseNode(ctx->globalNodeGraph.get(), cb);
    }
    for (auto& entry : ctx->geomNodes) {
        TraverseNode(entry.second.get(), cb);
    }
    for (auto& entry : ctx->freeStandingNodeGraphs) {
        if (entry.second) {
            TraverseNode(entry.second.get(), cb);
        }
    }
}

} // namespace anonymous

//------------------------------------------------------------------------------
// Loader
//------------------------------------------------------------------------------

RPRMtlxLoader::RPRMtlxLoader()
    : _stdSearchPath(mx::getEnvironmentPath()) {

}

RPRMtlxLoader::RenderableElements RPRMtlxLoader::GetRenderableElements(mx::Document const* mtlxDocument) {
    std::unordered_set<mx::Element*> processedElements;
    RenderableElements elements;
    ForEachRenderableElement(mtlxDocument,
        []() { return false; },
        [&](mx::ElementPtr const& mtlxElement) {
            if (processedElements.count(mtlxElement.get())) {
                return;
            }
            processedElements.insert(mtlxElement.get());

            bool elementOutputTypes[kOutputsTotal] = {};
            ForEachOutput(mtlxElement, [&elementOutputTypes](mx::OutputPtr const& output, mx::ShaderRefPtr const&) {
                auto outputType = ToOutputType(output->getType());
                if (outputType != kOutputNone) {
                    elementOutputTypes[outputType] = true;
                }

                return true;
            });

            std::string elementNamePath;
            for (int i = 0; i < kOutputsTotal; ++i) {
                if (elementOutputTypes[i]) {
                    if (elementNamePath.empty()) {
                        elementNamePath = mtlxElement->getNamePath();
                    }
                    elements.namePaths[i].push_back(elementNamePath);
                }
            }
        }
    );
    return elements;
}

RPRMtlxLoader::Result RPRMtlxLoader::Load(
    MaterialX::Document const* mtlxDocument,
    const std::string inputRenderableElements[kOutputsTotal],
    MaterialX::FileSearchPath const& searchPath,
    rpr_material_system rprMatSys) {

    LoaderContext ctx = {};
    ctx.logLevel = _logLevel;
    ctx.mtlxDocument = mtlxDocument;
    ctx.rprMatSys = rprMatSys;
    ctx.searchPath = searchPath;
    ctx.searchPath.append(_stdSearchPath);
    auto globalScope = ctx.EnterScope(LSGlobal, mtlxDocument);

    ctx.mtlxDocument->getUnitDefs();
    auto registerUnitType = [&ctx](const char* unitType, const char* sceneUnit) {
        if (auto unitTypeDef = ctx.mtlxDocument->getUnitTypeDef(unitType)) {
            for (auto& unitDef : unitTypeDef->getUnitDefs()) {
                LoaderContext::CompiledUnitType compiledUnitType;
                bool isValidUnits = true;
                for (auto& unit : unitDef->getUnits()) {
                    float scale = unit->getTypedAttribute<float>(SCALE_ATTRIBUTE);
                    if (scale == float{}) {
                        isValidUnits = false;
                        break;
                    }

                    compiledUnitType.scales[unit->getName()] = scale;
                }
                if (isValidUnits) {
                    auto it = compiledUnitType.scales.find(sceneUnit);
                    if (it == compiledUnitType.scales.end()) {
                        continue;
                    }

                    compiledUnitType.sceneScale = it->second;
                    ctx.compiledUnitTypes[unitType] = std::move(compiledUnitType);
                    return;
                }
            }
            LOG_ERROR(&ctx, "Could not find a valid unitDef for %s unitTypeDef", unitType);
        }
        LOG_ERROR(&ctx, "Unknown unitType: %s", unitType);
    };
    registerUnitType("distance", _sceneDistanceUnit.c_str());
    registerUnitType("angle", "radian");

    class MtlxRenderableElements {
    public:
        void Add(RPRMtlxLoader::OutputType outputType, mx::ElementPtr const& element, LoaderContext* ctx) {
            if (Exists(outputType)) {
                return;
            }

            ForEachOutput(element, [this, outputType](mx::OutputPtr const& output, mx::ShaderRefPtr const& shaderRef) {
                _Add(outputType, output, shaderRef);

                // Keep iterating until we find renderable element
                return !Exists(outputType);
            });
        }

        void Disable(RPRMtlxLoader::OutputType outputType) {
            _activeElementsBits |= 1u << outputType;
            _elements[outputType] = {};
        }

        bool IsEmpty() const {
            return _activeElementsBits == 0;
        }

        bool IsFull() const {
            return _activeElementsBits == kAllElementsBits;
        }

        bool Exists(RPRMtlxLoader::OutputType outputType) {
            if (outputType == RPRMtlxLoader::kOutputAny) {
                return IsFull();
            }
            return _activeElementsBits & (1u << outputType);
        }

        struct Element {
            mx::OutputPtr output;
            mx::ShaderRefPtr shaderRef;
        };
        Element const& Get(RPRMtlxLoader::OutputType type) const { return _elements[type]; }

    private:
        void _Add(RPRMtlxLoader::OutputType outputType, mx::OutputPtr const& output, mx::ShaderRefPtr const& shaderRef) {
            // Validate output type
            //
            auto actualOutputType = ToOutputType(output->getType());
            if (actualOutputType != RPRMtlxLoader::kOutputNone) {

                // If we are looking for a particular output type,
                // discard all outputs with a different type
                //
                if (outputType != RPRMtlxLoader::kOutputAny &&
                    outputType != actualOutputType) {
                    return;
                }

                uint8_t outputTypeBit = 1u << actualOutputType;
                if ((_activeElementsBits & outputTypeBit) == 0) {
                    _elements[actualOutputType] = {output, shaderRef};
                    _activeElementsBits |= outputTypeBit;
                }
            }
        }

    private:
        Element _elements[RPRMtlxLoader::kOutputsTotal];

        uint8_t _activeElementsBits = 0u;
        enum { kAllElementsBits = (1u << RPRMtlxLoader::kOutputsTotal) - 1 };
    };
    MtlxRenderableElements renderableElements;

    // Process user's renderable elements
    //
    if (inputRenderableElements) {
        for (int i = 0; i < kOutputsTotal; ++i) {
            auto outputType = static_cast<OutputType>(i);

            auto& namePath = inputRenderableElements[i];
            if (namePath.empty()) {
                renderableElements.Disable(outputType);
            } else {
                // XXX: getDescendant does not change the object, so it should be marked const
                //
                auto doc = const_cast<mx::Document*>(mtlxDocument);

                if (auto element = doc->getDescendant(namePath)) {
                    renderableElements.Add(outputType, element, &ctx);
                }
            }
        }
    }

    // If no renderable elements were specified or if they are invalid,
    // use first available renderable elements in the mtlxDocument
    //
    if (renderableElements.IsEmpty()) {
        ForEachRenderableElement(mtlxDocument,
            [&]() { return renderableElements.IsFull(); },
            [&](mx::ElementPtr const& element) { renderableElements.Add(kOutputAny, element, &ctx); }
        );
    }

    if (renderableElements.IsEmpty()) {
        LOG_ERROR(&ctx, "No renderable elements in %s", mtlxDocument->getSourceUri().c_str());
        return {};
    }

    // A few renderable elements may base on the same nodeGraph-shaderRef pair,
    // in this case, we want to create appropriate MtlxNodeGraphNode only once
    //
    GraphNodes graphNodes;

    for (int i = 0; i < kOutputsTotal; ++i) {
        auto outputType = static_cast<OutputType>(i);

        auto& element = renderableElements.Get(outputType);
        if (!element.output) {
            continue;
        }

        mx::ConstGraphElementPtr nodeGraph;
        if (element.shaderRef) {
            if (auto nodeDef = element.shaderRef->getNodeDef()) {
                nodeGraph = GetNodeGraphImpl(nodeDef.get());
            }
        } else {
            nodeGraph = element.output->getParent()->asA<mx::GraphElement>();
        }
        if (!nodeGraph) {
            continue;
        }

        GraphNodesKey graphNodesKey{nodeGraph.get(), element.shaderRef.get()};
        graphNodes[graphNodesKey].outputTypes.push_back(outputType);
    }

    // Create MtlxNodeGraphNode for each nodeGraph-shaderRef pair
    //
    bool hasAnyOutput = false;
    RprNode* rprOutputs[kOutputsTotal] = {};
    for (auto& entry : graphNodes) {
        std::vector<mx::OutputPtr> requiredOutputs;
        for (auto outputType : entry.second.outputTypes) {
            requiredOutputs.push_back(renderableElements.Get(outputType).output);
        }

        try {
            auto nodeGraph = entry.first.nodeGraph->getSelf()->asA<mx::GraphElement>();

            entry.second.node = std::make_unique<MtlxNodeGraphNode>(nodeGraph, requiredOutputs, &ctx);

            if (entry.first.shaderRef) {
                ForEachChildrenOfType<mx::BindInput>(entry.first.shaderRef,
                    []() { return false; },
                    [&ctx, node=entry.second.node.get()](mx::BindInputPtr const& bindInput) {
                        if (ctx.ConnectToGlobalOutput(bindInput.get(), node)) {
                            return;
                        }

                        auto& valueStr = bindInput->getValueString();
                        if (!valueStr.empty()) {
                            auto& type = bindInput->getType();

                            LOG(&ctx, "Bindinput %s: %s (%s)", bindInput->getName().c_str(), valueStr.c_str(), type.c_str());

                            node->SetInput(bindInput.get(), bindInput.get(), &ctx);
                        }
                    }
                );
            }
        } catch (MtlxNodeGraphNode::NoOutputsError&) {
            continue;
        }

        for (size_t i = 0; i < requiredOutputs.size(); ++i) {
            auto& output = requiredOutputs[i];

            auto it = entry.second.node->subNodes.find(output->getNodeName());
            if (it != entry.second.node->subNodes.end()) {
                RprNode* rprOutput = nullptr;
                if (auto rprNode = it->second->AsA<RprNode>()) {
                    rprOutput = rprNode;
                } else if (auto nodeGraphNode = it->second->AsA<MtlxNodeGraphNode>()) {
                    if (auto nodeGraphOutput = GetOutput(nodeGraphNode->mtlxGraph.get(), output.get(), &ctx)) {
                        if (auto subNode = nodeGraphNode->GetSubNode(nodeGraphOutput->getNodeName(), &ctx)) {
                            rprOutput = subNode->AsA<RprNode>();
                        }
                    }
                }

                // Nodes with non-shader type cannot be used directly as a surface output
                //
                if (entry.second.outputTypes[i] == kOutputSurface &&
                    rprOutput && rprOutput->rprNode) {

                    auto& outputType = output->getType();
                    if (outputType != "surfaceshader") {
                        entry.second.wrapNode = std::make_unique<RprWrapNode>(&ctx);
                        rprOutput->Connect(mx::EMPTY_STRING, entry.second.wrapNode.get(), nullptr, &ctx);
                        rprOutput = entry.second.wrapNode.get();
                    }
                }

                if (rprOutput && rprOutput->rprNode) {
                    auto outputType = entry.second.outputTypes[i];
                    rprOutputs[outputType] = rprOutput;
                    hasAnyOutput = true;
                }
            }
        }
    }
    if (!hasAnyOutput) {
        return {};
    }

    // Convert graphNodes to Result

    Result ret = {};

    // Calculate the total number of rpr nodes
    //
    TraverseGraphNodes(graphNodes, &ctx, [&ret](Node* node) {
        ret.numNodes += node->MoveRprApiHandles(nullptr);
    });

    ret.nodes = new rpr_material_node[ret.numNodes];

    // Map rpr_material_node handles to output type. We use it to fill rootNodeIndices
    //
    std::unordered_map<rpr_material_node, OutputType> targetOutputs;
    for (int i = 0; i < kOutputsTotal; ++i) {
        if (rprOutputs[i] && rprOutputs[i]->rprNode) {
            targetOutputs.emplace(rprOutputs[i]->rprNode, static_cast<OutputType>(i));
        }
    }

    // Move all rpr nodes from subnodes into the return array
    //
    std::vector<RPRMtlxLoader::Result::ImageNode> outImageNodes;
    size_t nodeOffset = 0;
    TraverseGraphNodes(graphNodes, &ctx, [&ret, &nodeOffset, &outImageNodes](Node* node) {
        // Export ImageNode
        //
        if (auto imageNode = node->AsA<RprImageNode>()) {
            if (!imageNode->resolvedFilepath.empty()) {
                outImageNodes.emplace_back();
                auto& outImageNode = outImageNodes.back();

                std::swap(outImageNode.type, imageNode->type);
                std::swap(outImageNode.file, imageNode->resolvedFilepath);
                std::swap(outImageNode.layer, imageNode->layer);
                std::swap(outImageNode.defaultValue, imageNode->defaultValue);
                std::swap(outImageNode.uaddressmode, imageNode->uaddressmode);
                std::swap(outImageNode.vaddressmode, imageNode->vaddressmode);
                outImageNode.disableRprImageColorspace = imageNode->disableRprImageColorspace;
                outImageNode.rprNode = imageNode->rprNode;
            }
        }

        nodeOffset += node->MoveRprApiHandles(ret.nodes + nodeOffset);
    });

    // Fill root node indices
    //
    for (auto& index : ret.rootNodeIndices) {
        index = Result::kInvalidRootNodeIndex;
    }
    for (size_t i = 0; i < ret.numNodes; ++i) {
        auto it = targetOutputs.find(ret.nodes[i]);
        if (it != targetOutputs.end()) {
            ret.rootNodeIndices[it->second] = i;
            targetOutputs.erase(it);
        }
    }

    if (!outImageNodes.empty()) {
        ret.numImageNodes = outImageNodes.size();
        ret.imageNodes = new Result::ImageNode[ret.numImageNodes];
        for (size_t i = 0; i < ret.numImageNodes; ++i) {
            ret.imageNodes[i] = outImageNodes[i];
        }
    }

    return ret;
}

void RPRMtlxLoader::SetupStdlib(mx::FilePathVec const& libraryNames, mx::FileSearchPath const& searchPath) {
    _stdlib = mx::createDocument();
    auto includes = mx::loadLibraries(libraryNames, searchPath, _stdlib);
    if (!includes.empty()) {
        _stdSearchPath.append(searchPath);
    }
}
