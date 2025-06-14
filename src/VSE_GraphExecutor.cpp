#include "VSE_GraphExecutor.h"
#include "VSE_NodeRegistry.h" // Though not directly used here, often related
#include <stdexcept>          // For runtime_error

namespace VSE
{
    GraphExecutor::GraphExecutor(Graph *graphToExecute)
        : m_Graph(graphToExecute)
    {
        if (!m_Graph)
        {
            VSE_CORE_CRITICAL("GraphExecutor: Graph to execute cannot be null.");
            throw std::invalid_argument("GraphExecutor: Graph to execute cannot be null.");
        }
        VSE_CORE_INFO("GraphExecutor created for graph '{}'.", m_Graph->GetName());
    }

    GraphExecutor::~GraphExecutor()
    {
        VSE_CORE_INFO("GraphExecutor for graph '{}' destroyed.", m_Graph ? m_Graph->GetName() : "UNKNOWN");
    }

    void GraphExecutor::ResetExecutionState()
    {
        VSE_CORE_TRACE("GraphExecutor: Resetting execution state.");
        m_CurrentValueCache.clear();
        m_CurrentNodeEvalStates.clear();
        while (!m_ExecutionStack.empty())
            m_ExecutionStack.pop();
        m_ActiveDataResolutionStack.clear();
        // Note: This does NOT reset m_Graph->ResetNodesTransientState().
        // That should be done by the owner of the graph if a full reset is desired.
        // This executor reset is for a single TriggerEvent's state.
    }

    void GraphExecutor::TriggerEvent(const VSE_ID &entryNodeID, const std::string &entryExecPinName)
    {
        ResetExecutionState();               // Ensure a clean state for this trigger
        m_Graph->ResetNodesTransientState(); // Also reset pin current values etc. on the graph itself

        Node *startNode = m_Graph->GetNode(entryNodeID);
        if (!startNode)
        {
            VSE_CORE_ERROR("GraphExecutor::TriggerEvent: Entry node ID '{}' not found in graph '{}'.", entryNodeID, m_Graph->GetName());
            return;
        }

        VSE_CORE_INFO("GraphExecutor: Triggering event on Node '{}' (ID: {}), Graph '{}'.", startNode->Title, entryNodeID, m_Graph->GetName());

        // If it's an event node that might not have input exec pins, or if no specific pin is given,
        // we can push it directly.
        // If an entryExecPinName is provided, we'd ideally find that pin and see what it's connected to
        // to start the flow, but for a typical "event" node, it's the node itself that starts.

        // For now, let's assume event nodes are pushed directly.
        // More sophisticated entry point handling (e.g., via a specific input exec pin) can be added.
        if (startNode->Definition->bIsEventTrigger || entryExecPinName.empty())
        {
            m_ExecutionStack.push(startNode);
        }
        else
        {
            Pin *entryPin = startNode->FindPinByName(entryExecPinName, PinDirection::Input);
            if (entryPin && entryPin->GetType() == PinType::Execution)
            {
                // If the entry pin itself should be "triggered" (e.g. it represents an incoming exec flow)
                // This logic depends on how you model events vs. callable subgraphs.
                // For now, pushing the node is sufficient for simple events.
                m_ExecutionStack.push(startNode); // Still push the node, its Execute will handle the flow.
            }
            else
            {
                VSE_CORE_ERROR("GraphExecutor::TriggerEvent: Entry exec pin '{}' not found or not an execution pin on Node '{}'.", entryExecPinName, startNode->Title);
                return;
            }
        }

        // Main execution loop
        while (!m_ExecutionStack.empty())
        {
            Node *currentNodeToExecute = m_ExecutionStack.top();
            m_ExecutionStack.pop();

            if (!currentNodeToExecute)
                continue; // Should not happen if stack is managed well

            ExecuteNode(currentNodeToExecute);
        }
        VSE_CORE_INFO("GraphExecutor: Event trigger on Node '{}' completed.", startNode->Title);
    }

    void GraphExecutor::ExecuteNode(Node *node)
    {
        if (!node || !node->Definition)
            return;

        VSE_CORE_TRACE("GraphExecutor: Entering Node '{}' (Type: {}).", node->Title, node->Definition->TypeName);
        if (OnNodeEnter)
            OnNodeEnter(node);

        // Create execution context for this node
        ExecutionContext ctx(this, m_Graph, node);

        // Execute the node's logic
        int outputExecPinIndex = -1;
        if (node->Definition->Execute)
        {
            try
            {
                outputExecPinIndex = node->Definition->Execute(ctx);
            }
            catch (const std::exception &e)
            {
                VSE_CORE_CRITICAL("GraphExecutor: Exception during execution of Node '{}' (Type: {}): {}",
                                  node->Title, node->Definition->TypeName, e.what());
                if (OnNodeExit)
                    OnNodeExit(node); // Still call exit if possible
                // Potentially stop further graph execution here or mark graph as faulted
                return; // Stop processing this branch
            }
        }
        else if (node->Definition->bIsPure)
        {
            // Pure nodes might not have an "Execute" in the traditional sense if all inputs are connected.
            // Their values are resolved on demand by ResolvePinValue.
            // If a pure node *is* on the execution stack, it means an exec pin led to it.
            // It might have an exec output to pass through.
            // For now, assume pure nodes with exec pins have a simple pass-through Execute or return 0.
            VSE_CORE_TRACE("GraphExecutor: Pure Node '{}' encountered in execution path. Assuming pass-through if it has exec outputs.", node->Title);
            // If it has output exec pins, it should have an Execute function that returns an index.
            // If it's pure and only has data pins, it shouldn't normally be on m_ExecutionStack.
        }
        else
        {
            VSE_CORE_WARN("GraphExecutor: Node '{}' (Type: {}) has no Execute function and is not marked pure. No action taken.", node->Title, node->Definition->TypeName);
        }

        VSE_CORE_TRACE("GraphExecutor: Exiting Node '{}'. Output exec index: {}", node->Title, outputExecPinIndex);
        if (OnNodeExit)
            OnNodeExit(node);

        // If the execution resulted in an output execution pin index, find and follow it.
        if (outputExecPinIndex >= 0)
        {
            Pin *outputExecPin = FindOutputExecutionPinByIndex(node, outputExecPinIndex);
            if (outputExecPin)
            {
                if (!outputExecPin->ConnectedLinkIDs.empty())
                {
                    // An output execution pin should ideally only connect to one input exec pin.
                    // If multiple, behavior might be undefined or execute all (like a sequence).
                    // For now, assume the first valid connected link.
                    for (const VSE_ID &linkID : outputExecPin->ConnectedLinkIDs)
                    {
                        Link *link = m_Graph->GetLink(linkID);
                        if (link)
                        {
                            Pin *nextInputExecPin = m_Graph->GetPin(link->ToPinID);
                            if (nextInputExecPin && nextInputExecPin->ParentNode)
                            {
                                VSE_CORE_TRACE("GraphExecutor: Following exec link ID '{}' from '{}' to '{}' on Node '{}'.",
                                               link->ID, outputExecPin->GetName(), nextInputExecPin->GetName(), nextInputExecPin->ParentNode->Title);
                                if (OnExecLinkTraversed)
                                    OnExecLinkTraversed(link);
                                m_ExecutionStack.push(nextInputExecPin->ParentNode);
                                // If multiple exec outputs are allowed from one pin (e.g. multicast),
                                // you'd push all. But typically, an exec output pin has one outgoing wire.
                                // If NodeDefinition::Execute returns an index for a *specific* pin, and that
                                // pin fans out, all connected nodes are pushed. This is like UE's Sequence.
                                // If the Execute implies a *single choice*, then the output pin itself should not fan out.
                            }
                        }
                    }
                }
                else
                {
                    VSE_CORE_TRACE("GraphExecutor: Output exec pin '{}' on Node '{}' is not connected.", outputExecPin->GetName(), node->Title);
                }
            }
            else
            {
                VSE_CORE_WARN("GraphExecutor: Node '{}' requested output exec pin index {}, but no such pin was found.", node->Title, outputExecPinIndex);
            }
        }
    }

    Pin *GraphExecutor::FindOutputExecutionPinByIndex(Node *node, int index) const
    {
        if (!node || index < 0)
            return nullptr;
        int currentIndex = 0;
        for (Pin &pin : node->OutputPins)
        {
            if (pin.GetType() == PinType::Execution)
            {
                if (currentIndex == index)
                {
                    return &pin;
                }
                currentIndex++;
            }
        }
        return nullptr;
    }

    Variant GraphExecutor::ResolvePinValue(Pin *inputDataPin, ExecutionContext & /*ctx*/) // ctx might be needed if resolution involves complex logic dependent on current node
    {
        if (!inputDataPin || inputDataPin->GetType() != PinType::Data || inputDataPin->GetDirection() != PinDirection::Input)
        {
            VSE_CORE_ERROR("GraphExecutor::ResolvePinValue: Invalid pin provided or not an input data pin.");
            return Variant(); // Return empty variant
        }

        Node *resolvingNode = inputDataPin->ParentNode; // The node that *owns* this input pin.

        // 1. Check if value is already cached for this input pin's *source*
        //    (This is slightly complex: input pins don't "have" values, their connected output pins do)
        //    For now, let's assume the value is cached by the *output pin ID* that produces it.
        //    If an input pin is connected, its "value" is the value of the output pin it's connected to.

        // 2. If connected, resolve the source output pin's value.
        if (!inputDataPin->ConnectedLinkIDs.empty())
        {
            // Data input pins should typically only have one connection.
            // If more, use the first one (or define merging behavior).
            VSE_ID linkID = inputDataPin->ConnectedLinkIDs[0];
            Link *link = m_Graph->GetLink(linkID);
            if (!link)
            {
                VSE_CORE_ERROR("GraphExecutor::ResolvePinValue: Link ID '{}' not found for Pin '{}'.", linkID, inputDataPin->ID);
                return inputDataPin->GetEffectiveDefaultValue(); // Fallback
            }

            Pin *sourceOutputPin = m_Graph->GetPin(link->FromPinID);
            if (!sourceOutputPin || sourceOutputPin->GetType() != PinType::Data || sourceOutputPin->GetDirection() != PinDirection::Output)
            {
                VSE_CORE_ERROR("GraphExecutor::ResolvePinValue: Source pin for Link ID '{}' is invalid or not an output data pin.", linkID);
                return inputDataPin->GetEffectiveDefaultValue(); // Fallback
            }

            Node *upstreamNode = sourceOutputPin->ParentNode;

            // Check if sourceOutputPin's value is already in the global cache for this run
            auto cacheIt = m_CurrentValueCache.find(sourceOutputPin->ID);
            if (cacheIt != m_CurrentValueCache.end())
            {
                VSE_CORE_TRACE("GraphExecutor: Value for Pin '{}' (Node '{}') found in cache.", sourceOutputPin->GetName(), upstreamNode->Title);
                if (OnPinValueResolved)
                    OnPinValueResolved(sourceOutputPin, cacheIt->second);
                return cacheIt->second;
            }

            // Value not cached. We need to evaluate the upstream node.
            // Cycle detection for data dependencies:
            auto evalStateIt = m_CurrentNodeEvalStates.find(upstreamNode->ID);
            if (evalStateIt != m_CurrentNodeEvalStates.end())
            {
                if (evalStateIt->second == EvaluationState::Evaluating)
                {
                    VSE_CORE_CRITICAL("GraphExecutor: Cycle detected! Node '{}' (Type: {}) is already being evaluated while trying to resolve data for Node '{}'.",
                                      upstreamNode->Title, upstreamNode->Definition->TypeName, resolvingNode->Title);
                    throw std::runtime_error("Data dependency cycle detected involving node: " + upstreamNode->Title);
                }
                // If 'Evaluated', its output pin values should be in m_CurrentValueCache.
                // This case should have been caught by the cacheIt check above unless something is wrong.
            }

            if (m_ActiveDataResolutionStack.count(upstreamNode))
            {
                VSE_CORE_CRITICAL("GraphExecutor: Cycle detected (via active stack)! Node '{}' (Type: {}) is already in active data resolution stack.",
                                  upstreamNode->Title, upstreamNode->Definition->TypeName);
                throw std::runtime_error("Data dependency cycle detected (active stack) involving node: " + upstreamNode->Title);
            }

            VSE_CORE_TRACE("GraphExecutor: Resolving upstream Node '{}' (Type: {}) for Pin '{}'.",
                           upstreamNode->Title, upstreamNode->Definition->TypeName, sourceOutputPin->GetName());

            m_CurrentNodeEvalStates[upstreamNode->ID] = EvaluationState::Evaluating;
            m_ActiveDataResolutionStack.insert(upstreamNode);

            // "Execute" the upstream node to calculate its outputs.
            // Pure nodes calculate outputs directly. Non-pure nodes might also have data outputs
            // that are calculated as part of their main Execute, or have a separate data-eval path.
            // For now, assume that calling ExecuteNode also populates its data outputs.
            // This implies that even "data-only" nodes need an Execute function in their definition
            // that computes outputs and caches them.
            if (upstreamNode->Definition->Execute)
            {
                ExecutionContext upstreamCtx(this, m_Graph, upstreamNode); // Create context for the upstream node
                // We don't care about the exec output index here, only that it calculates data outputs.
                upstreamNode->Definition->Execute(upstreamCtx);
            }
            else if (!upstreamNode->Definition->bIsPure)
            {
                VSE_CORE_WARN("GraphExecutor: Upstream node '{}' (Type: {}) has no Execute function and is not pure. Cannot resolve data outputs.",
                              upstreamNode->Title, upstreamNode->Definition->TypeName);
            }
            // If it's pure and has no Execute, its values should be resolved on demand.
            // This part of the logic might need refinement: how exactly do pure nodes without explicit
            // exec pins get their values calculated if not through an Execute() call?
            // The current model implies that *any* node whose output is needed will have its Execute() called.

            m_ActiveDataResolutionStack.erase(upstreamNode);
            m_CurrentNodeEvalStates[upstreamNode->ID] = EvaluationState::Evaluated;

            // After upstream node execution, its output value should be in the cache.
            cacheIt = m_CurrentValueCache.find(sourceOutputPin->ID);
            if (cacheIt != m_CurrentValueCache.end())
            {
                if (OnPinValueResolved)
                    OnPinValueResolved(sourceOutputPin, cacheIt->second);
                return cacheIt->second;
            }
            else
            {
                VSE_CORE_ERROR("GraphExecutor: Upstream Node '{}' executed, but Pin '{}' value not found in cache.",
                               upstreamNode->Title, sourceOutputPin->GetName());
                // Fallback to input pin's default if upstream failed to provide value
                return inputDataPin->GetEffectiveDefaultValue();
            }
        }

        // 3. Not connected: use the input pin's default value.
        VSE_CORE_TRACE("GraphExecutor: Input Pin '{}' (Node '{}') is unconnected. Using default value.",
                       inputDataPin->GetName(), resolvingNode->Title);
        Variant defaultValue = inputDataPin->GetEffectiveDefaultValue();
        if (OnPinValueResolved)
            OnPinValueResolved(inputDataPin, defaultValue); // Call for default too
        return defaultValue;
    }

    void GraphExecutor::CacheOutputPinValue(Pin *outputDataPin, Variant value, ExecutionContext & /*ctx*/)
    {
        if (!outputDataPin || outputDataPin->GetType() != PinType::Data || outputDataPin->GetDirection() != PinDirection::Output)
        {
            VSE_CORE_ERROR("GraphExecutor::CacheOutputPinValue: Invalid pin or not an output data pin.");
            return;
        }

        // Ensure value type matches pin's data type, or attempt conversion
        if (outputDataPin->GetDataType() && value.HasValue() && value.GetTypeInfo() &&
            !outputDataPin->GetDataType()->IsSame(*value.GetTypeInfo()))
        {
            Variant convertedValue = value.ConvertTo(*outputDataPin->GetDataType(), TypeRegistry::Instance());
            if (convertedValue.HasValue())
            {
                value = convertedValue;
            }
            else
            {
                VSE_CORE_ERROR("GraphExecutor::CacheOutputPinValue: Failed to convert value from type '{}' to pin type '{}' for Pin '{}' on Node '{}'. Value not cached.",
                               value.GetTypeInfo() ? value.GetTypeInfo()->Name : "Unknown",
                               outputDataPin->GetDataType()->Name,
                               outputDataPin->GetName(),
                               outputDataPin->ParentNode->Title);
                return; // Don't cache if conversion fails
            }
        }

        VSE_CORE_TRACE("GraphExecutor: Caching value for Output Pin '{}' (ID: {}) on Node '{}'.",
                       outputDataPin->GetName(), outputDataPin->ID, outputDataPin->ParentNode->Title);
        m_CurrentValueCache[outputDataPin->ID] = value; // Store by output pin's instance ID

        // Also update the pin's CurrentValue and ValueState directly for inspection
        outputDataPin->CurrentValue = value;
        outputDataPin->ValueState = EvaluationState::Evaluated;

        if (OnPinValueCached)
            OnPinValueCached(outputDataPin, value);
    }

    Variant GraphExecutor::GetGraphVariableValue(const std::string &variableName, Graph *graph)
    {
        if (!graph)
        {
            VSE_CORE_ERROR("GraphExecutor::GetGraphVariableValue: Graph is null.");
            return Variant();
        }
        GraphVariable *var = graph->GetVariableByName(variableName);
        if (var)
        {
            return var->Value;
        }
        VSE_CORE_WARN("GraphExecutor::GetGraphVariableValue: Variable '{}' not found in graph '{}'.", variableName, graph->GetName());
        return Variant();
    }

    bool GraphExecutor::SetGraphVariableValue(const std::string &variableName, Variant value, Graph *graph)
    {
        if (!graph)
        {
            VSE_CORE_ERROR("GraphExecutor::SetGraphVariableValue: Graph is null.");
            return false;
        }
        GraphVariable *var = graph->GetVariableByName(variableName);
        if (var)
        {
            // Type check/conversion before assignment
            if (var->DataType && value.HasValue() && value.GetTypeInfo() && !var->DataType->IsSame(*value.GetTypeInfo()))
            {
                Variant convertedValue = value.ConvertTo(*var->DataType, TypeRegistry::Instance());
                if (convertedValue.HasValue())
                {
                    var->Value = convertedValue;
                    VSE_CORE_TRACE("GraphExecutor::SetGraphVariableValue: Converted and set variable '{}'.", variableName);
                    return true;
                }
                else
                {
                    VSE_CORE_ERROR("GraphExecutor::SetGraphVariableValue: Type mismatch for variable '{}'. Expected '{}', got '{}', and conversion failed.",
                                   variableName, var->DataType->Name, value.GetTypeInfo()->Name);
                    return false;
                }
            }
            var->Value = value; // Assign directly if types match or no conversion needed
            VSE_CORE_TRACE("GraphExecutor::SetGraphVariableValue: Set variable '{}'.", variableName);
            return true;
        }
        VSE_CORE_ERROR("GraphExecutor::SetGraphVariableValue: Variable '{}' not found in graph '{}'.", variableName, graph->GetName());
        return false;
    }

} // namespace VSE

// --- Now, we need to implement the ExecutionContext methods that use the GraphExecutor ---
// This typically goes in VSE_ExecutionContext.cpp, or can be inlined in VSE_ExecutionContext.h
// if GraphExecutor is fully defined before ExecutionContext's method definitions.
// For clarity, let's assume a VSE_ExecutionContext.cpp or put it at the end of VSE_GraphExecutor.cpp

// In VSE_GraphExecutor.cpp (or a new VSE_ExecutionContext.cpp if you prefer)
namespace VSE
{

    Variant ExecutionContext::GetInputValue(const std::string &pinName)
    {
        if (!CurrentNode)
        {
            VSE_CORE_ERROR("ExecutionContext::GetInputValue: CurrentNode is null.");
            return Variant();
        }
        Pin *inputPin = CurrentNode->FindPinByName(pinName, PinDirection::Input);
        if (!inputPin)
        {
            VSE_CORE_WARN("ExecutionContext::GetInputValue: Input pin '{}' not found on Node '{}'.", pinName, CurrentNode->Title);
            return Variant();
        }
        if (inputPin->GetType() != PinType::Data)
        {
            VSE_CORE_WARN("ExecutionContext::GetInputValue: Pin '{}' on Node '{}' is not a Data pin.", pinName, CurrentNode->Title);
            return Variant(); // Or handle specifically if exec pin values can be "gotten" (unusual)
        }
        return Executor->ResolvePinValue(inputPin, *this);
    }

    void ExecutionContext::SetOutputValue(const std::string &pinName, Variant value)
    {
        if (!CurrentNode)
        {
            VSE_CORE_ERROR("ExecutionContext::SetOutputValue: CurrentNode is null.");
            return;
        }
        Pin *outputPin = CurrentNode->FindPinByName(pinName, PinDirection::Output);
        if (!outputPin)
        {
            VSE_CORE_WARN("ExecutionContext::SetOutputValue: Output pin '{}' not found on Node '{}'.", pinName, CurrentNode->Title);
            return;
        }
        if (outputPin->GetType() != PinType::Data)
        {
            VSE_CORE_WARN("ExecutionContext::SetOutputValue: Pin '{}' on Node '{}' is not a Data pin.", pinName, CurrentNode->Title);
            return;
        }
        Executor->CacheOutputPinValue(outputPin, std::move(value), *this);
    }

    Variant ExecutionContext::GetGraphVariable(const std::string &variableName)
    {
        if (!Executor)
        {
            VSE_CORE_ERROR("ExecutionContext::GetGraphVariable: Executor is null.");
            return Variant();
        }
        return Executor->GetGraphVariableValue(variableName, CurrentGraph);
    }

    bool ExecutionContext::SetGraphVariable(const std::string &variableName, Variant value)
    {
        if (!Executor)
        {
            VSE_CORE_ERROR("ExecutionContext::SetGraphVariable: Executor is null.");
            return false;
        }
        return Executor->SetGraphVariableValue(variableName, std::move(value), CurrentGraph);
    }

} // namespace VSE