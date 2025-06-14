#pragma once
#include "VSE_ID.h"
#include "VSE_Types.h" // For Variant, TypeRegistry, etc.
// Forward declarations for types it will interact with.
// Full includes will be in the .cpp files of users of ExecutionContext (like GraphExecutor).
namespace VSE
{
    class Graph;
    struct Node;         // Instance
    struct Pin;          // Instance
    class GraphExecutor; // The entity running the execution

    // If you have complex value caching or state tracking during execution,
    // those might be forward-declared here and owned/managed by GraphExecutor.
    // For now, we'll assume simple direct interactions or through GraphExecutor.
}

namespace VSE
{
    struct ExecutionContext
    {
        GraphExecutor *Executor; // Non-owning pointer to the currently running executor.
        Graph *CurrentGraph;     // Non-owning pointer to the graph being executed.
        Node *CurrentNode;       // Non-owning pointer to the node currently being executed.

        // --- Constructor ---
        ExecutionContext(GraphExecutor *executor, Graph *graph, Node *node)
            : Executor(executor), CurrentGraph(graph), CurrentNode(node)
        {
            // Null checks can be added here if desired, or handled by the caller (GraphExecutor)
            // For example:
            // if (!Executor || !CurrentGraph || !CurrentNode) {
            //     VSE_CORE_CRITICAL("ExecutionContext: Executor, CurrentGraph, or CurrentNode is null!");
            //     // Decide on error handling: throw, or allow but expect crashes if used.
            // }
        }

        // --- Input Value Retrieval ---

        // Gets the value for an input data pin by its name.
        // This will trigger evaluation of upstream nodes if necessary.
        // Returns an empty Variant if the pin is not found, not a data pin,
        // or if value resolution fails.
        Variant GetInputValue(const std::string &pinName);

        // Templated version for convenience, with type checking and default value.
        template <typename T>
        T GetInputValue(const std::string &pinName, const T &defaultValue)
        {
            Variant variantValue = GetInputValue(pinName);
            if (variantValue.HasValue())
            {
                // Attempt to get the value as type T
                // If direct type match:
                if (variantValue.GetTypeInfo() && variantValue.GetTypeInfo()->IsSame(*TypeRegistry::Get<T>()))
                {
                    return variantValue.GetValue<T>();
                }
                // Else, attempt conversion:
                Variant converted = variantValue.ConvertTo(*TypeRegistry::Get<T>(), TypeRegistry::Instance());
                if (converted.HasValue())
                {
                    return converted.GetValue<T>();
                }
                VSE_CORE_WARN("ExecutionContext: Pin '{}' value type '{}' could not be converted to requested type '{}' for Node '{}'. Using default.",
                              pinName,
                              variantValue.GetTypeInfo() ? variantValue.GetTypeInfo()->Name : "Unknown",
                              TypeRegistry::Get<T>()->Name,
                              CurrentNode->Definition->TypeName);
            }
            return defaultValue;
        }

        // --- Output Value Setting ---

        // Sets the value for an output data pin by its name.
        // The executor will typically store this in a value cache.
        void SetOutputValue(const std::string &pinName, Variant value);

        // Templated version for convenience.
        template <typename T>
        void SetOutputValue(const std::string &pinName, T &&value)
        {
            SetOutputValue(pinName, Variant(std::forward<T>(value)));
        }

        // --- Execution Flow Control (Advanced - for nodes like Branch, Loop) ---

        // Signals to the executor to follow a specific output execution pin by its index.
        // This is an alternative/complement to the integer returned by NodeDefinition::Execute.
        // Could be useful if a node's logic decides mid-way which path to take.
        // For now, NodeDefinition::Execute returning an int is the primary mechanism.
        // void SignalExecOutput(int outputExecPinIndex);

        // --- Access to Graph Variables ---
        Variant GetGraphVariable(const std::string &variableName);

        template <typename T>
        T GetGraphVariable(const std::string &variableName, const T &defaultValue)
        {
            Variant var = GetGraphVariable(variableName);
            if (var.HasValue())
            {
                if (var.GetTypeInfo() && var.GetTypeInfo()->IsSame(*TypeRegistry::Get<T>()))
                {
                    return var.GetValue<T>();
                }
                Variant converted = var.ConvertTo(*TypeRegistry::Get<T>(), TypeRegistry::Instance());
                if (converted.HasValue())
                {
                    return converted.GetValue<T>();
                }
                VSE_CORE_WARN("ExecutionContext: Graph variable '{}' type '{}' could not be converted to requested type '{}'. Using default.",
                              variableName,
                              var.GetTypeInfo() ? var.GetTypeInfo()->Name : "Unknown",
                              TypeRegistry::Get<T>()->Name);
            }
            return defaultValue;
        }

        bool SetGraphVariable(const std::string &variableName, Variant value);

        template <typename T>
        bool SetGraphVariable(const std::string &variableName, T &&value)
        {
            return SetGraphVariable(variableName, Variant(std::forward<T>(value)));
        }

        // --- Logging (Convenience) ---
        // These would just call VSE_CORE_... macros but could prefix with current node info.
        // void LogTrace(const std::string& message);
        // void LogInfo(const std::string& message);
        // void LogWarn(const std::string& message);
        // void LogError(const std::string& message);

        // Note: The actual implementation of GetInputValue and SetOutputValue will
        // typically delegate to methods on the GraphExecutor, which manages the
        // value cache and the process of resolving upstream nodes.
        // This struct is primarily an interface for the node's Execute function.
    };

} // namespace VSE