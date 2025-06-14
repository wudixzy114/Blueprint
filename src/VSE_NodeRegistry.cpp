#include "VSE_NodeRegistry.h"
#include "VSE_Logger.h" // Already included via VSE_NodeDefinition.h, but good for explicitness
#include <algorithm>    // For std::sort and std::unique on categories

namespace VSE
{
    NodeRegistry &NodeRegistry::Instance()
    {
        static NodeRegistry instance;
        return instance;
    }

    NodeRegistry::NodeRegistry()
    {
        VSE_CORE_INFO("NodeRegistry Initialized.");
    }

    NodeRegistry::~NodeRegistry()
    {
        VSE_CORE_INFO("NodeRegistry Destroyed.");
    }

    bool NodeRegistry::RegisterDefinition(NodeDefinition definition) // Pass by value, then move
    {
        const std::string &typeName = definition.TypeName; // Get typeName before move
        if (typeName.empty())
        {
            VSE_CORE_ERROR("NodeRegistry: Attempted to register a NodeDefinition with an empty TypeName.");
            return false;
        }

        auto it = m_Definitions.find(typeName);
        if (it != m_Definitions.end())
        {
            VSE_CORE_WARN("NodeRegistry: NodeDefinition with TypeName '{}' already exists. Registration failed.", typeName);
            return false;
        }

        // Validate pin names uniqueness within the definition before registering
        // Input Pins
        std::vector<std::string> inputPinNames;
        for (const auto &pinDef : definition.InputPins)
        {
            if (std::find(inputPinNames.begin(), inputPinNames.end(), pinDef.Name) != inputPinNames.end())
            {
                VSE_CORE_ERROR("NodeRegistry: NodeDefinition '{}' has duplicate input pin name '{}'. Registration failed.", typeName, pinDef.Name);
                return false;
            }
            inputPinNames.push_back(pinDef.Name);
        }
        // Output Pins
        std::vector<std::string> outputPinNames;
        for (const auto &pinDef : definition.OutputPins)
        {
            if (std::find(outputPinNames.begin(), outputPinNames.end(), pinDef.Name) != outputPinNames.end())
            {
                VSE_CORE_ERROR("NodeRegistry: NodeDefinition '{}' has duplicate output pin name '{}'. Registration failed.", typeName, pinDef.Name);
                return false;
            }
            outputPinNames.push_back(pinDef.Name);
        }

        VSE_CORE_INFO("NodeRegistry: Registering NodeDefinition '{}' in category '{}'.", typeName, definition.Category);
        m_Definitions.emplace(typeName, std::move(definition)); // Move the definition into the map
        return true;
    }

    bool NodeRegistry::UnregisterDefinition(const std::string &typeName)
    {
        auto it = m_Definitions.find(typeName);
        if (it != m_Definitions.end())
        {
            VSE_CORE_INFO("NodeRegistry: Unregistering NodeDefinition '{}'.", typeName);
            m_Definitions.erase(it);
            return true;
        }
        VSE_CORE_WARN("NodeRegistry: Could not find NodeDefinition with TypeName '{}' to unregister.", typeName);
        return false;
    }

    const NodeDefinition *NodeRegistry::GetDefinition(const std::string &typeName) const
    {
        auto it = m_Definitions.find(typeName);
        if (it != m_Definitions.end())
        {
            return &it->second;
        }
        // VSE_CORE_TRACE("NodeRegistry: NodeDefinition with TypeName '{}' not found.", typeName); // Can be noisy
        return nullptr;
    }

    std::vector<const NodeDefinition *> NodeRegistry::GetAllDefinitions() const
    {
        std::vector<const NodeDefinition *> result;
        result.reserve(m_Definitions.size());
        for (const auto &pair : m_Definitions)
        {
            result.push_back(&pair.second);
        }
        return result;
    }

    std::vector<const NodeDefinition *> NodeRegistry::GetDefinitionsByCategory(const std::string &category) const
    {
        std::vector<const NodeDefinition *> result;
        for (const auto &pair : m_Definitions)
        {
            if (pair.second.Category == category)
            {
                result.push_back(&pair.second);
            }
        }
        return result;
    }

    std::vector<std::string> NodeRegistry::GetAllCategories() const
    {
        std::vector<std::string> categories;
        categories.reserve(m_Definitions.size()); // Max possible categories is num definitions
        for (const auto &pair : m_Definitions)
        {
            categories.push_back(pair.second.Category);
        }

        // Sort and remove duplicates
        std::sort(categories.begin(), categories.end());
        categories.erase(std::unique(categories.begin(), categories.end()), categories.end());

        return categories;
    }

} // namespace VSE