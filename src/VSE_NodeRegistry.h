#pragma once
#include "VSE_NodeDefinition.h" // Includes VSE_ID.h, VSE_Types.h, VSE_Logger.h
#include <string>
#include <vector>
#include <map>
#include <memory> // For std::unique_ptr if NodeDefinition becomes heap-allocated (not strictly necessary now)

namespace VSE
{
    class NodeRegistry
    {
    public:
        static NodeRegistry &Instance();

        NodeRegistry(const NodeRegistry &) = delete;
        NodeRegistry &operator=(const NodeRegistry &) = delete;

        // Registers a node definition.
        // Takes ownership if NodeDefinition were heap-allocated (e.g. std::unique_ptr<NodeDefinition>).
        // For stack/member NodeDefinitions, it copies. We'll store by value for now.
        // Returns true on successful registration, false if a definition with the same TypeName already exists.
        bool RegisterDefinition(NodeDefinition definition); // Pass by value to allow moving into map

        // Unregisters a node definition by its TypeName.
        // Returns true if a definition was found and removed, false otherwise.
        bool UnregisterDefinition(const std::string &typeName);

        // Retrieves a node definition by its TypeName.
        // Returns nullptr if not found.
        const NodeDefinition *GetDefinition(const std::string &typeName) const;

        // Retrieves all registered node definitions.
        // Useful for populating UI lists of available nodes.
        std::vector<const NodeDefinition *> GetAllDefinitions() const;

        // Retrieves all registered node definitions within a specific category.
        std::vector<const NodeDefinition *> GetDefinitionsByCategory(const std::string &category) const;

        // Retrieves a list of all unique category names.
        std::vector<std::string> GetAllCategories() const;

    private:
        NodeRegistry(); // Private constructor for singleton
        ~NodeRegistry();

        // Stores NodeDefinition objects by their TypeName.
        // NodeDefinition is stored by value. If it becomes very large or needs polymorphic behavior
        // (though unlikely for definitions), std::unique_ptr<NodeDefinition> could be used.
        std::map<std::string, NodeDefinition> m_Definitions;
    };
} // namespace VSE