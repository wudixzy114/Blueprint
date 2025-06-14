#include "VSE_GraphExecutor.h"
#include "VSE_NodeRegistry.h"
#include <stdexcept>

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