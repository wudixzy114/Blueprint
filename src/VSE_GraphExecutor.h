#pragma once
#include "VSE_GraphData.h"        // Includes Node, Pin, Link, Graph, VSE_ID, VSE_Types
#include "VSE_ExecutionContext.h" // The context passed to nodes
#include "VSE_Logger.h"
#include <stack>
#include <map>
#include <set>
#include <functional> // For callbacks

namespace VSE
{
    // Manages the execution of a VSE Graph.
    class GraphExecutor
    {
    public:
        // Struct to hold cached values during a single execution run.
        // Pin instance ID -> Variant value.
        using ValueCache = std::map<VSE_ID, Variant>;

        // Struct to hold the evaluation state of nodes during data resolution.
        // Node instance ID -> EvaluationState.
        using NodeEvaluationStateMap = std::map<VSE_ID, EvaluationState>;

        // Callbacks for debugging/visualization (optional)
        using NodeEventCallback = std::function<void(Node *node)>;
        using LinkEventCallback = std::function<void(Link *link)>;
        using PinValueEventCallback = std::function<void(Pin *pin, const Variant &value)>;

        explicit GraphExecutor(Graph *graphToExecute);
        ~GraphExecutor();

        GraphExecutor(const GraphExecutor &) = delete;
        GraphExecutor &operator=(const GraphExecutor &) = delete;

        // Triggers graph execution starting from a specific "event" node and its primary input execution pin.
        // If entryNode has no input exec pins (e.g. an "OnBeginPlay" type event), it's executed directly.
        // If entryExecPinName is empty, it tries to find a default input exec pin or just executes the node.
        void TriggerEvent(const VSE_ID &entryNodeID, const std::string &entryExecPinName = "");

        // Resets the executor's internal state (caches, etc.) for a fresh run.
        // Graph::ResetNodesTransientState() should also be called externally if needed.
        void ResetExecutionState();

        // --- Methods called by ExecutionContext (or internally) ---
        // These are the implementations for ExecutionContext's Get/Set methods.

        // Resolves the value for a given INPUT data pin.
        // This is the core of the pull-based data evaluation.
        Variant ResolvePinValue(Pin *inputDataPin, ExecutionContext &ctx);

        // Stores an output value from the currently executing node into the cache.
        void CacheOutputPinValue(Pin *outputDataPin, Variant value, ExecutionContext &ctx);

        // Retrieves a graph variable's value.
        Variant GetGraphVariableValue(const std::string &variableName, Graph *graph);

        // Sets a graph variable's value.
        bool SetGraphVariableValue(const std::string &variableName, Variant value, Graph *graph);

        // --- Debugging Callbacks ---
        NodeEventCallback OnNodeEnter;            // Called before a node's Execute function.
        NodeEventCallback OnNodeExit;             // Called after a node's Execute function.
        LinkEventCallback OnExecLinkTraversed;    // Called when an execution link is followed.
        PinValueEventCallback OnPinValueResolved; // Called when a data pin's value is resolved.
        PinValueEventCallback OnPinValueCached;   // Called when an output pin's value is cached.

    private:
        Graph *m_Graph; // Non-owning pointer to the graph being executed.

        // State for a single TriggerEvent run:
        ValueCache m_CurrentValueCache;                 // Caches values of data pins for the current execution.
        NodeEvaluationStateMap m_CurrentNodeEvalStates; // Tracks node states for data dependency cycle detection.
        std::stack<Node *> m_ExecutionStack;            // Stack of nodes whose execution pins are to be processed.
        std::set<Node *> m_ActiveDataResolutionStack;   // For detecting cycles during data pin resolution.

        // Helper to find the Nth output execution pin of a node.
        Pin *FindOutputExecutionPinByIndex(Node *node, int index) const;

        // Core execution loop for a single node.
        void ExecuteNode(Node *node);
    };

} // namespace VSE