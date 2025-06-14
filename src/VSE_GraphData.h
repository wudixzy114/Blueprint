#pragma once
#include "VSE_ID.h"
#include "VSE_Types.h"          // For PinType, PinDirection, Variant, EvaluationState, TypeRegistry
#include "VSE_NodeDefinition.h" // For NodeDefinition, PinDefinition
#include "VSE_Logger.h"
#include <string>
#include <vector>
#include <map>
#include <memory> // For std::unique_ptr

namespace VSE
{
    // Forward declarations
    class Node;
    class Graph;

    // Instance of a Pin on a Node. Created based on a PinDefinition.
    struct Pin
    {
        VSE_ID ID;                       // Unique ID for this pin *instance* in the graph.
        Node *ParentNode;                // Non-owning pointer to the Node this pin belongs to.
        const PinDefinition *Definition; // Non-owning pointer to its blueprint.

        // Instance-specific override for the default value.
        // If this Variant HasValue(), it's used instead of Definition->DefaultValue.
        Variant DefaultValueOverride;

        // For data pins, this caches the last computed/resolved value during an execution run.
        // For output data pins, it's set by the node's Execute function.
        // For input data pins, it's resolved by the executor before the node's Execute is called.
        // This state is typically transient per execution trigger.
        mutable Variant CurrentValue;
        mutable EvaluationState ValueState = EvaluationState::NotEvaluated; // For data pins

        // Stores IDs of Links connected to this pin.
        // Helps in finding connections without iterating all graph links.
        std::vector<VSE_ID> ConnectedLinkIDs;

        Pin(VSE_ID id, Node *parent, const PinDefinition *definition)
            : ID(std::move(id)), ParentNode(parent), Definition(definition)
        {
            if (!ParentNode)
                VSE_CORE_CRITICAL("Pin '{}': ParentNode cannot be null.", Definition ? Definition->Name : "UNNAMED_PIN_DEF_NULL");
            if (!Definition)
                VSE_CORE_CRITICAL("Pin: Definition cannot be null.");
        }

        // --- Convenience Accessors ---
        const std::string &GetName() const { return Definition->Name; }
        PinType GetType() const { return Definition->Type; }
        PinDirection GetDirection() const { return Definition->Direction; }
        const TypeInfo *GetDataType() const { return Definition->DataType; }
        const std::string &GetTooltip() const { return Definition->Tooltip; }
        bool IsRequired() const { return Definition->bIsRequired; }

        Variant GetEffectiveDefaultValue() const
        {
            if (DefaultValueOverride.HasValue())
            {
                // Ensure override type matches definition, or attempt conversion
                if (Definition->DataType && DefaultValueOverride.GetTypeInfo() &&
                    !Definition->DataType->IsSame(*DefaultValueOverride.GetTypeInfo()))
                {
                    Variant converted = DefaultValueOverride.ConvertTo(*Definition->DataType, TypeRegistry::Instance());
                    if (converted.HasValue())
                        return converted;
                    VSE_CORE_WARN("Pin '{} ({})': DefaultValueOverride type mismatch and couldn't convert. Falling back to PinDefinition default.", GetName(), ID);
                }
                else
                {
                    return DefaultValueOverride;
                }
            }
            return Definition->DefaultValue; // Fallback to definition's default
        }

        // Resets transient state (called by executor before a new run)
        void ResetTransientState()
        {
            if (GetType() == PinType::Data)
            {
                CurrentValue.Reset();
                ValueState = EvaluationState::NotEvaluated;
            }
        }
    };

    // Instance of a Node in the Graph. Created based on a NodeDefinition.
    struct Node
    {
        VSE_ID ID;                        // Unique ID for this node *instance*.
        Graph *ParentGraph;               // Non-owning pointer to the Graph this node belongs to.
        const NodeDefinition *Definition; // Non-owning pointer to its blueprint.

        std::string Title;              // Instance-specific title. Can override Definition->Title.
                                        // If empty, UI might use Definition->Title.
        float PosX = 0.0f, PosY = 0.0f; // Position on the graph canvas.

        // Pins owned by this node instance.
        // These are created based on Definition->InputPins and Definition->OutputPins.
        std::vector<Pin> InputPins;
        std::vector<Pin> OutputPins;

        Variant InternalState; // For stateful nodes (e.g., flip-flop, delay).
                               // Initialized by Definition->CreateInitialState if provided.

        Node(VSE_ID id, Graph *parentGraph, const NodeDefinition *definition)
            : ID(std::move(id)), ParentGraph(parentGraph), Definition(definition)
        {
            if (!ParentGraph)
                VSE_CORE_CRITICAL("Node: ParentGraph cannot be null.");
            if (!Definition)
                VSE_CORE_CRITICAL("Node: Definition cannot be null for Node ID '{}'.", ID);

            Title = Definition->Title; // Default instance title to definition's title

            // Create Pin instances from PinDefinitions
            for (const auto &pinDef : Definition->InputPins)
            {
                InputPins.emplace_back(IDGenerator::Generate(), this, &pinDef);
            }
            for (const auto &pinDef : Definition->OutputPins)
            {
                OutputPins.emplace_back(IDGenerator::Generate(), this, &pinDef);
            }

            // Initialize internal state
            if (Definition->CreateInitialState)
            {
                InternalState = Definition->CreateInitialState();
            }
        }

        Pin *FindPinByID(const VSE_ID &pinID)
        {
            for (auto &pin : InputPins)
            {
                if (pin.ID == pinID)
                    return &pin;
            }
            for (auto &pin : OutputPins)
            {
                if (pin.ID == pinID)
                    return &pin;
            }
            return nullptr;
        }

        const Pin *FindPinByID(const VSE_ID &pinID) const
        {
            for (const auto &pin : InputPins)
            {
                if (pin.ID == pinID)
                    return &pin;
            }
            for (const auto &pin : OutputPins)
            {
                if (pin.ID == pinID)
                    return &pin;
            }
            return nullptr;
        }

        Pin *FindPinByName(const std::string &name, PinDirection direction)
        {
            if (direction == PinDirection::Input)
            {
                for (auto &pin : InputPins)
                {
                    if (pin.GetName() == name)
                        return &pin;
                }
            }
            else
            {
                for (auto &pin : OutputPins)
                {
                    if (pin.GetName() == name)
                        return &pin;
                }
            }
            return nullptr;
        }
        const Pin *FindPinByName(const std::string &name, PinDirection direction) const
        {
            if (direction == PinDirection::Input)
            {
                for (const auto &pin : InputPins)
                {
                    if (pin.GetName() == name)
                        return &pin;
                }
            }
            else
            {
                for (const auto &pin : OutputPins)
                {
                    if (pin.GetName() == name)
                        return &pin;
                }
            }
            return nullptr;
        }

        // Resets transient state of all pins (called by executor)
        void ResetPinsTransientState()
        {
            for (auto &pin : InputPins)
                pin.ResetTransientState();
            for (auto &pin : OutputPins)
                pin.ResetTransientState();
        }
    };

    // Represents a connection between two Pins in the Graph.
    struct Link
    {
        VSE_ID ID;        // Unique ID for this link *instance*.
        VSE_ID FromPinID; // ID of the output pin instance.
        VSE_ID ToPinID;   // ID of the input pin instance.
        // VSE_ID FromNodeID; // Redundant if we always lookup pins via PinID, but can be useful for quick node access
        // VSE_ID ToNodeID;   // Redundant

        Link(VSE_ID id, VSE_ID fromPinID, VSE_ID toPinID)
            : ID(std::move(id)), FromPinID(std::move(fromPinID)), ToPinID(std::move(toPinID)) {}
    };

    // Represents a variable stored within a Graph.
    struct GraphVariable
    {
        VSE_ID ID;                // Unique ID for this variable *instance*.
        std::string Name;         // User-defined name for the variable.
        const TypeInfo *DataType; // Type of the variable.
        Variant Value;            // Current value of the variable.
        std::string Tooltip;
        // bool bIsExposedToEditor = true;
        // bool bIsReadOnly = false;

        GraphVariable(VSE_ID id, std::string name, const TypeInfo *type, Variant initialValue, std::string tooltip = "")
            : ID(std::move(id)), Name(std::move(name)), DataType(type), Value(std::move(initialValue)), Tooltip(std::move(tooltip))
        {
            if (!DataType)
                VSE_CORE_ERROR("GraphVariable '{}': DataType cannot be null.", Name);
            if (Value.HasValue() && DataType && Value.GetTypeInfo() && !DataType->IsSame(*Value.GetTypeInfo()))
            {
                Variant convertedValue = Value.ConvertTo(*DataType, TypeRegistry::Instance());
                if (convertedValue.HasValue())
                {
                    Value = convertedValue;
                    VSE_CORE_TRACE("GraphVariable '{}': InitialValue was converted to match DataType {}.", Name, DataType->Name);
                }
                else
                {
                    VSE_CORE_ERROR("GraphVariable '{}': InitialValue type '{}' does not match DataType '{}' and couldn't be converted. Value will be default.",
                                   Name, Value.GetTypeInfo() ? Value.GetTypeInfo()->Name : "Unknown", DataType->Name);
                    Value.Reset(); // Or assign a default Variant of DataType
                    // this->Value = Variant(); // TODO: How to create default variant of specific type?
                    // For now, let it be an empty variant if conversion fails.
                }
            }
        }
    };

    // The main container for a visual script.
    // Owns Nodes, Links, and Variables.
    class Graph
    {
    public:
        Graph(std::string name = "New Graph", VSE_ID id = IDGenerator::Generate());
        ~Graph();

        // Prevent copying and assignment for now, as ownership of unique_ptrs makes it tricky.
        // Implement if needed with careful deep copying.
        Graph(const Graph &) = delete;
        Graph &operator=(const Graph &) = delete;
        // Consider move constructor/assignment
        Graph(Graph &&) = default; // Default move ops should work with unique_ptrs
        Graph &operator=(Graph &&) = default;

        const VSE_ID &GetID() const { return m_ID; }
        const std::string &GetName() const { return m_Name; }
        void SetName(const std::string &name) { m_Name = name; }

        // --- Node Management ---
        // Creates a Node instance from a NodeDefinition in the NodeRegistry.
        // Returns nullptr if nodeTypeName is not found or creation fails.
        // desiredNodeID, if provided and valid, will be used; otherwise, a new ID is generated.
        Node *AddNodeFromDefinition(const std::string &nodeTypeName, float posX = 0.0f, float posY = 0.0f, VSE_ID desiredNodeID = VSE_ID());
        bool RemoveNode(const VSE_ID &nodeID); // Also removes connected links.

        Node *GetNode(const VSE_ID &nodeID);
        const Node *GetNode(const VSE_ID &nodeID) const;
        const std::vector<std::unique_ptr<Node>> &GetNodes() const { return m_Nodes; }

        // --- Pin Management (indirectly via Nodes) ---
        Pin *GetPin(const VSE_ID &pinID);
        const Pin *GetPin(const VSE_ID &pinID) const;

        // --- Link Management ---
        // Validates pin compatibility before creating and adding the link.
        // Returns nullptr if validation fails or pins are not found.
        Link *AddLink(const VSE_ID &fromPinID, const VSE_ID &toPinID, VSE_ID desiredLinkID = VSE_ID());
        bool RemoveLink(const VSE_ID &linkID);
        const std::vector<std::unique_ptr<Link>> &GetLinks() const { return m_Links; }
        Link *GetLink(const VSE_ID &linkID);
        const Link *GetLink(const VSE_ID &linkID) const;
        std::vector<const Link *> GetLinksConnectedToPin(const VSE_ID &pinID) const;
        std::vector<const Link *> GetLinksConnectedToNode(const VSE_ID &nodeID) const;

        // --- Variable Management ---
        GraphVariable *AddVariable(const std::string &name, const TypeInfo *type, Variant initialValue = Variant(), std::string tooltip = "", VSE_ID desiredVarID = VSE_ID());
        bool RemoveVariable(const VSE_ID &varID);
        GraphVariable *GetVariable(const VSE_ID &varID);
        const GraphVariable *GetVariable(const VSE_ID &varID) const;
        GraphVariable *GetVariableByName(const std::string &name);
        const GraphVariable *GetVariableByName(const std::string &name) const;
        const std::vector<std::unique_ptr<GraphVariable>> &GetVariables() const { return m_Variables; }

        // --- Utilities ---
        // Call this to ensure all internal lookup maps are up-to-date.
        // Needed after deserialization or complex direct manipulations (if any).
        // AddNode/RemoveNode etc. should maintain maps incrementally.
        void RebuildLookups();

        // Resets transient state of all nodes and their pins.
        void ResetNodesTransientState();

    private:
        VSE_ID m_ID;
        std::string m_Name;

        std::vector<std::unique_ptr<Node>> m_Nodes;
        std::vector<std::unique_ptr<Link>> m_Links;
        std::vector<std::unique_ptr<GraphVariable>> m_Variables;

        // Lookup maps for quick access (pointers are non-owning).
        // These are populated/updated by Add/Remove methods and RebuildLookups().
        std::map<VSE_ID, Node *> m_NodeMap;
        std::map<VSE_ID, Pin *> m_PinMap; // Maps pin instance ID to Pin*
        std::map<VSE_ID, Link *> m_LinkMap;
        std::map<VSE_ID, GraphVariable *> m_VariableMap;

        // Helper to remove a link and update Pin::ConnectedLinkIDs
        void EraseLinkInternal(typename std::vector<std::unique_ptr<Link>>::iterator linkIt);
        bool ValidatePinConnection(const Pin *fromPin, const Pin *toPin) const;
    };

} // namespace VSE