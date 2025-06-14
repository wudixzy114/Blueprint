#pragma once
#include "VSE_ID.h"
#include "VSE_Types.h" // For PinType, PinDirection, TypeInfo, Variant
#include "VSE_Logger.h"
#include <string>
#include <vector>
#include <functional> // For std::function in NodeDefinition
#include <map>

namespace VSE
{
    class Node;                  // Instance of a node
    class Graph;                 // For context in some definition functions
    struct ExecutionContext;     // For the Execute function signature
    struct StaticAnalysisReport; // For the StaticValidate function signature (to be defined later)
}

namespace VSE
{
    // Describes a single pin on a node blueprint.
    // This is the *definition*, not the instance on a Node.
    struct PinDefinition
    {
        VSE_ID ID;                // A unique ID for this pin *definition* (e.g., generated when definition is created)
        std::string Name;         // User-visible name (e.g., "Value", "Exec In", "True", "A")
                                  // Should be unique per direction (Input/Output) within a NodeDefinition.
        PinType Type;             // Execution, Data
        PinDirection Direction;   // Input, Output
        const TypeInfo *DataType; // Null if PinType::Execution. Points to TypeInfo in TypeRegistry.
        Variant DefaultValue;     // Only if Direction == Input && Type == Data.
                                  // Must match DataType if DataType is not null.
        std::string Tooltip;      // Optional tooltip for the UI.
        bool bIsRequired = true;  // For input data pins: if true, static analysis should warn if unconnected.
                                  // For exec input pins: usually true.
                                  // For exec output pins: usually false (not all exec outs need to be connected).
        // std::map<std::string, Variant> Metadata; // For custom properties, colors, etc.
        PinDefinition(
            VSE_ID id,
            std::string name,
            PinType type,
            PinDirection direction,
            const TypeInfo *dataType = nullptr,
            Variant defaultValue = Variant(), // Default construct Variant if no specific default
            std::string tooltip = "",
            bool isRequired = true)
            : ID(std::move(id)), Name(std::move(name)), Type(type), Direction(direction),
              DataType(dataType), DefaultValue(std::move(defaultValue)),
              Tooltip(std::move(tooltip)), bIsRequired(isRequired)
        {
            if (Type == PinType::Data && DataType == nullptr)
            {
                VSE_CORE_ERROR("PinDefinition '{}': Data pins must have a DataType.", Name);
                // Consider throwing here or having a "valid" flag
            }
            if (Type == PinType::Execution && DataType != nullptr)
            {
                VSE_CORE_WARN("PinDefinition '{}': Execution pins should not have a DataType. It will be ignored.", Name);
                DataType = nullptr; // Enforce
            }
            if (Direction == PinDirection::Input && Type == PinType::Data && DefaultValue.HasValue())
            {
                if (DataType && DefaultValue.GetTypeInfo() && !DataType->IsSame(*DefaultValue.GetTypeInfo()))
                {
                    // Attempt conversion, or warn/error
                    Variant convertedDefault = DefaultValue.ConvertTo(*DataType, TypeRegistry::Instance());
                    if (convertedDefault.HasValue())
                    {
                        DefaultValue = convertedDefault;
                        VSE_CORE_TRACE("PinDefinition '{}': DefaultValue was converted to match DataType {}.", Name, DataType->Name);
                    }
                    else
                    {
                        VSE_CORE_ERROR("PinDefinition '{}': DefaultValue type '{}' does not match DataType '{}' and couldn't be converted.",
                                       Name, DefaultValue.GetTypeInfo() ? DefaultValue.GetTypeInfo()->Name : "Unknown", DataType->Name);
                        // Consider clearing DefaultValue or throwing
                        DefaultValue.Reset();
                    }
                }
            }
            if (Direction == PinDirection::Output && DefaultValue.HasValue())
            {
                VSE_CORE_WARN("PinDefinition '{}': DefaultValue on Output pins is not typically used. It will be ignored.", Name);
                DefaultValue.Reset(); // Enforce
            }
        }
    };

    struct NodeDefinition
    {
        std::string TypeName;    // Unique programmatic identifier (e.g., "math.add_int", "events.on_begin_play")
        std::string Title;       // Default title for new node instances (e.g., "Add Integer", "Begin Play")
        std::string Category;    // For UI grouping (e.g., "Math/Arithmetic", "Events")
        std::string Description; // Detailed description for UI/help.

        std::vector<PinDefinition> InputPins;
        std::vector<PinDefinition> OutputPins;

        // The core logic.
        // Returns the index of the *Output Execution Pin* to follow.
        // - Return >= 0: Index of the output execution pin in `OutputPins` to activate.
        // - Return -1:  No further execution flow from this node (e.g., data node, or end of exec chain).
        // Specific negative values could be reserved for error codes if needed, but -1 is standard for "stop".
        std::function<int(ExecutionContext &)> Execute;

        // Optional: Custom function for static validation of a node instance based on this definition.
        // The Node* argument is the actual instance in the graph.
        // The StaticAnalysisReport& is where messages (errors, warnings) are added.
        std::function<void(const Node *nodeInstance, const Graph *graph, StaticAnalysisReport &report)> StaticValidateInstance;

        // Optional: Function to create initial internal state for a new node instance.
        // Called when a Node is created from this definition.
        std::function<Variant()> CreateInitialState;

        bool bIsCallable = true;      // Can this node be executed directly (i.e., has an 'Execute' function)? Pure data nodes might be false.
        bool bIsEventTrigger = false; // Is this a node that can start an execution flow spontaneously (e.g., "OnKeyPress")?
                                      // If true, it usually has no input execution pins.
        bool bIsPure = false;         // For data nodes: if true, implies no side effects and output depends only on inputs.
                                      // UI can choose not to draw exec pins for pure nodes if all inputs are connected.

        // std::map<std::string, Variant> Metadata; // For custom properties, node color, icon hints, etc.

        NodeDefinition(
            std::string typeName,
            std::string title,
            std::string category,
            std::function<int(ExecutionContext &)> executeFunc) : TypeName(std::move(typeName)), Title(std::move(title)), Category(std::move(category)),
                                                                  Execute(std::move(executeFunc)), bIsCallable(true) // Default to callable if execute is provided
        {
            if (TypeName.empty())
                VSE_CORE_ERROR("NodeDefinition: TypeName cannot be empty.");
            if (Title.empty())
                Title = TypeName; // Default title to TypeName if empty
            if (!Execute && bIsCallable)
            {
                // If it's meant to be callable but no Execute function is provided, this is an issue.
                // Or, we could allow bIsCallable=true for nodes that only have data pins and are "executed" by data pull.
                // For now, let's assume if Execute is null, it's likely a pure data node or an event.
                // This logic might need refinement based on how pure nodes are handled.
                VSE_CORE_WARN("NodeDefinition '{}': Execute function is null, but bIsCallable is true. Consider setting bIsCallable to false if it's a data-only or event node not directly 'executed'.", TypeName);
                // bIsCallable = false; // Or let the user decide.
            }
        }

        // Helper to add an input pin definition
        PinDefinition &AddInputPin(
            const std::string &name,
            PinType type,
            const TypeInfo *dataType = nullptr, // Required if PinType::Data
            Variant defaultValue = Variant(),
            const std::string &tooltip = "",
            bool isRequired = true)
        {
            // Simple check for name uniqueness among input pins
            for (const auto &pin : InputPins)
            {
                if (pin.Name == name)
                {
                    VSE_CORE_ERROR("NodeDefinition '{}': Duplicate input pin name '{}'.", TypeName, name);
                    // Decide on error handling: throw, or return a reference to existing, or allow (bad idea)
                    // For now, let's just log and continue, but this should ideally be prevented.
                }
            }
            VSE_ID pinDefID = IDGenerator::Generate(); // Generate ID for the pin definition
            InputPins.emplace_back(pinDefID, name, type, PinDirection::Input, dataType, std::move(defaultValue), tooltip, isRequired);
            return InputPins.back();
        }

        // Helper to add an output pin definition
        PinDefinition &AddOutputPin(
            const std::string &name,
            PinType type,
            const TypeInfo *dataType = nullptr, // Required if PinType::Data
            const std::string &tooltip = "")
        {
            for (const auto &pin : OutputPins)
            {
                if (pin.Name == name)
                {
                    VSE_CORE_ERROR("NodeDefinition '{}': Duplicate output pin name '{}'.", TypeName, name);
                }
            }
            VSE_ID pinDefID = IDGenerator::Generate();
            // Output pins typically don't have default values or 'isRequired' in the same sense as inputs
            OutputPins.emplace_back(pinDefID, name, type, PinDirection::Output, dataType, Variant(), tooltip, false);
            return OutputPins.back();
        }

        const PinDefinition *FindInputPinDefinition(const std::string &name) const
        {
            for (const auto &pinDef : InputPins)
            {
                if (pinDef.Name == name)
                    return &pinDef;
            }
            return nullptr;
        }

        const PinDefinition *FindOutputPinDefinition(const std::string &name) const
        {
            for (const auto &pinDef : OutputPins)
            {
                if (pinDef.Name == name)
                    return &pinDef;
            }
            return nullptr;
        }

        const PinDefinition *FindPinDefinitionByID(const VSE_ID &id) const
        {
            for (const auto &pinDef : InputPins)
            {
                if (pinDef.ID == id)
                    return &pinDef;
            }
            for (const auto &pinDef : OutputPins)
            {
                if (pinDef.ID == id)
                    return &pinDef;
            }
            return nullptr;
        }
    };
}