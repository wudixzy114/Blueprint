#include "VSE_GraphData.h"
#include "VSE_NodeRegistry.h" // Needed for AddNodeFromDefinition
#include "VSE_Logger.h"
#include <algorithm> // For std::remove_if, std::find_if
#include <set>

namespace VSE
{
    // --- Graph Implementation ---
    Graph::Graph(std::string name, VSE_ID id)
        : m_ID(id.empty() ? IDGenerator::Generate() : std::move(id)),
          m_Name(std::move(name))
    {
        VSE_CORE_INFO("Graph '{}' (ID: {}) created.", m_Name, m_ID);
    }

    Graph::~Graph()
    {
        VSE_CORE_INFO("Graph '{}' (ID: {}) destroyed.", m_Name, m_ID);
        // unique_ptrs will handle cleanup of Nodes, Links, Variables
    }

    Node *Graph::AddNodeFromDefinition(const std::string &nodeTypeName, float posX, float posY, VSE_ID desiredNodeID)
    {
        const NodeDefinition *def = NodeRegistry::Instance().GetDefinition(nodeTypeName);
        if (!def)
        {
            VSE_CORE_ERROR("Graph::AddNode: NodeDefinition '{}' not found in registry.", nodeTypeName);
            return nullptr;
        }

        VSE_ID newNodeID = desiredNodeID.empty() ? IDGenerator::Generate() : std::move(desiredNodeID);
        if (m_NodeMap.count(newNodeID))
        {
            VSE_CORE_ERROR("Graph::AddNode: Node with desired ID '{}' already exists.", newNodeID);
            // Optionally, try generating a new ID if desiredID was provided and clashed
            // newNodeID = IDGenerator::Generate(); // If you want to auto-fix
            // if (m_NodeMap.count(newNodeID)) return nullptr; // If still clash (unlikely)
            return nullptr;
        }

        // Create the node (std::make_unique calls Node constructor)
        auto newNode = std::make_unique<Node>(newNodeID, this, def);
        newNode->PosX = posX;
        newNode->PosY = posY;

        Node *rawPtr = newNode.get();
        m_Nodes.push_back(std::move(newNode));
        m_NodeMap[rawPtr->ID] = rawPtr;

        // Add its pins to the pin map
        for (auto &pin : rawPtr->InputPins)
        {
            m_PinMap[pin.ID] = &pin;
        }
        for (auto &pin : rawPtr->OutputPins)
        {
            m_PinMap[pin.ID] = &pin;
        }

        VSE_CORE_INFO("Graph '{}': Added Node '{}' (Type: {}, ID: {}).", m_Name, rawPtr->Title, nodeTypeName, rawPtr->ID);
        return rawPtr;
    }

    bool Graph::RemoveNode(const VSE_ID &nodeID)
    {
        auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(),
                               [&](const auto &nodePtr)
                               { return nodePtr->ID == nodeID; });

        if (it == m_Nodes.end())
        {
            VSE_CORE_WARN("Graph::RemoveNode: Node with ID '{}' not found.", nodeID);
            return false;
        }

        Node *nodeToRemove = it->get();
        VSE_CORE_INFO("Graph '{}': Removing Node '{}' (ID: {}).", m_Name, nodeToRemove->Title, nodeID);

        // Remove links connected to this node
        std::vector<VSE_ID> linksToPurge;
        std::set<VSE_ID> uniqueLinkIDsToPurge; // Use a set to avoid duplicates if a link somehow involves two pins on the same node (though rare/invalid)
        for (const auto &pin : nodeToRemove->InputPins)
        {
            for (const VSE_ID &linkID : pin.ConnectedLinkIDs)
            {
                uniqueLinkIDsToPurge.insert(linkID);
            }
        }
        for (const auto &pin : nodeToRemove->OutputPins)
        {
            for (const VSE_ID &linkID : pin.ConnectedLinkIDs)
            {
                uniqueLinkIDsToPurge.insert(linkID);
            }
        }
        for (const VSE_ID &linkID : uniqueLinkIDsToPurge)
        {
            RemoveLink(linkID);
        }

        // Remove pins from pin map
        for (const auto &pin : nodeToRemove->InputPins)
        {
            m_PinMap.erase(pin.ID);
        }
        for (const auto &pin : nodeToRemove->OutputPins)
        {
            m_PinMap.erase(pin.ID);
        }

        // Remove from node map and vector
        m_NodeMap.erase(nodeID);
        m_Nodes.erase(it);

        return true;
    }

    Node *Graph::GetNode(const VSE_ID &nodeID)
    {
        auto it = m_NodeMap.find(nodeID);
        return (it != m_NodeMap.end()) ? it->second : nullptr;
    }
    const Node *Graph::GetNode(const VSE_ID &nodeID) const
    {
        auto it = m_NodeMap.find(nodeID);
        return (it != m_NodeMap.end()) ? it->second : nullptr;
    }

    Pin *Graph::GetPin(const VSE_ID &pinID)
    {
        auto it = m_PinMap.find(pinID);
        return (it != m_PinMap.end()) ? it->second : nullptr;
    }
    const Pin *Graph::GetPin(const VSE_ID &pinID) const
    {
        auto it = m_PinMap.find(pinID);
        return (it != m_PinMap.end()) ? it->second : nullptr;
    }

    bool Graph::ValidatePinConnection(const Pin *fromPin, const Pin *toPin) const
    {
        if (!fromPin || !toPin)
        {
            VSE_CORE_ERROR("AddLink: One or both pins are null.");
            return false;
        }

        // 1. Direction check
        if (fromPin->GetDirection() != PinDirection::Output || toPin->GetDirection() != PinDirection::Input)
        {
            VSE_CORE_ERROR("AddLink: Invalid pin directions. Must link Output to Input. From: {}, To: {}",
                           (int)fromPin->GetDirection(), (int)toPin->GetDirection());
            return false;
        }

        // 2. Type check (Exec to Exec, Data to Data)
        if (fromPin->GetType() != toPin->GetType())
        {
            VSE_CORE_ERROR("AddLink: Pin types mismatch. From: {}, To: {}", (int)fromPin->GetType(), (int)toPin->GetType());
            return false;
        }

        // 3. Data type compatibility (for Data pins)
        if (fromPin->GetType() == PinType::Data)
        {
            const TypeInfo *fromDataType = fromPin->GetDataType();
            const TypeInfo *toDataType = toPin->GetDataType();
            if (!fromDataType || !toDataType)
            {
                VSE_CORE_ERROR("AddLink: Data pins have null DataType. From: '{}', To: '{}'", fromPin->GetName(), toPin->GetName());
                return false;
            }
            // Check if 'toDataType' can be assigned from 'fromDataType'
            if (!toDataType->IsAssignableFrom(*fromDataType, TypeRegistry::Instance()))
            {
                VSE_CORE_ERROR("AddLink: Incompatible data types. Cannot assign from '{}' (Pin: {}) to '{}' (Pin: {}).",
                               fromDataType->Name, fromPin->GetName(), toDataType->Name, toPin->GetName());
                return false;
            }
        }

        // 4. Input pin connection limit (typically, data inputs allow one connection, exec inputs too)
        //    This rule can be relaxed by node definition flags later if needed.
        if (toPin->GetType() == PinType::Data && !toPin->ConnectedLinkIDs.empty())
        {
            VSE_CORE_WARN("AddLink: Input data pin '{}' on Node '{}' already has a connection. Replacing existing.",
                          toPin->GetName(), toPin->ParentNode->Title);
            // Optionally, remove existing link(s) to this input pin first
            // Or make this an error and disallow multiple inputs by default. For now, it's a warning.
        }
        if (toPin->GetType() == PinType::Execution && !toPin->ConnectedLinkIDs.empty())
        {
            VSE_CORE_WARN("AddLink: Input execution pin '{}' on Node '{}' already has a connection. Replacing existing.",
                          toPin->GetName(), toPin->ParentNode->Title);
        }

        // 5. Prevent self-connection (pin to itself)
        if (fromPin->ID == toPin->ID)
        {
            VSE_CORE_ERROR("AddLink: Cannot connect a pin to itself ('{}').", fromPin->ID);
            return false;
        }

        // 6. Prevent connecting pins on the same node if it's not logical (e.g. output of node to its own input)
        //    This might be allowed for specific "loop" nodes, but generally disallowed.
        if (fromPin->ParentNode == toPin->ParentNode)
        {
            VSE_CORE_WARN("AddLink: Connecting an output pin to an input pin on the same node ('{}'). This is often unintended.", fromPin->ParentNode->Title);
            // Could be an error depending on strictness.
        }

        return true;
    }

    Link *Graph::AddLink(const VSE_ID &fromPinID, const VSE_ID &toPinID, VSE_ID desiredLinkID)
    {
        Pin *fromPin = GetPin(fromPinID);
        Pin *toPin = GetPin(toPinID);

        if (!ValidatePinConnection(fromPin, toPin))
        {
            return nullptr; // Validation failed, logged in ValidatePinConnection
        }

        // If input pin already has connections, remove them first (common behavior)
        // For Data pins specifically, only one input is usually allowed.
        if ((toPin->GetType() == PinType::Data || toPin->GetType() == PinType::Execution) && !toPin->ConnectedLinkIDs.empty())
        {
            VSE_CORE_INFO("AddLink: Input pin '{}' already connected. Removing existing {} link(s).", toPin->GetName(), toPin->ConnectedLinkIDs.size());
            std::vector<VSE_ID> linksToClear = toPin->ConnectedLinkIDs; // Copy because RemoveLink modifies ConnectedLinkIDs
            for (const VSE_ID &linkIdToClear : linksToClear)
            {
                RemoveLink(linkIdToClear);
            }
        }

        VSE_ID newLinkID = desiredLinkID.empty() ? IDGenerator::Generate() : std::move(desiredLinkID);
        if (m_LinkMap.count(newLinkID))
        {
            VSE_CORE_ERROR("Graph::AddLink: Link with desired ID '{}' already exists.", newLinkID);
            return nullptr;
        }

        auto newLink = std::make_unique<Link>(newLinkID, fromPinID, toPinID);
        Link *rawPtr = newLink.get();

        m_Links.push_back(std::move(newLink));
        m_LinkMap[rawPtr->ID] = rawPtr;

        // Update connected link IDs on pins
        fromPin->ConnectedLinkIDs.push_back(rawPtr->ID);
        toPin->ConnectedLinkIDs.push_back(rawPtr->ID);

        VSE_CORE_INFO("Graph '{}': Added Link ID '{}' from Pin '{}' (Node '{}') to Pin '{}' (Node '{}').",
                      m_Name, rawPtr->ID, fromPin->GetName(), fromPin->ParentNode->Title, toPin->GetName(), toPin->ParentNode->Title);
        return rawPtr;
    }

    void Graph::EraseLinkInternal(typename std::vector<std::unique_ptr<Link>>::iterator linkIt)
    {
        Link *linkToRemove = linkIt->get();

        // Remove from Pin's ConnectedLinkIDs
        Pin *fromPin = GetPin(linkToRemove->FromPinID);
        if (fromPin)
        {
            fromPin->ConnectedLinkIDs.erase(
                std::remove(fromPin->ConnectedLinkIDs.begin(), fromPin->ConnectedLinkIDs.end(), linkToRemove->ID),
                fromPin->ConnectedLinkIDs.end());
        }
        Pin *toPin = GetPin(linkToRemove->ToPinID);
        if (toPin)
        {
            toPin->ConnectedLinkIDs.erase(
                std::remove(toPin->ConnectedLinkIDs.begin(), toPin->ConnectedLinkIDs.end(), linkToRemove->ID),
                toPin->ConnectedLinkIDs.end());
        }

        // Remove from map and vector
        m_LinkMap.erase(linkToRemove->ID);
        m_Links.erase(linkIt);
    }

    bool Graph::RemoveLink(const VSE_ID &linkID)
    {
        auto it = std::find_if(m_Links.begin(), m_Links.end(),
                               [&](const auto &linkPtr)
                               { return linkPtr->ID == linkID; });

        if (it == m_Links.end())
        {
            VSE_CORE_WARN("Graph::RemoveLink: Link with ID '{}' not found.", linkID);
            return false;
        }
        VSE_CORE_INFO("Graph '{}': Removing Link ID '{}'.", m_Name, linkID);
        EraseLinkInternal(it);
        return true;
    }

    Link *Graph::GetLink(const VSE_ID &linkID)
    {
        auto it = m_LinkMap.find(linkID);
        return (it != m_LinkMap.end()) ? it->second : nullptr;
    }
    const Link *Graph::GetLink(const VSE_ID &linkID) const
    {
        auto it = m_LinkMap.find(linkID);
        return (it != m_LinkMap.end()) ? it->second : nullptr;
    }

    std::vector<const Link *> Graph::GetLinksConnectedToPin(const VSE_ID &pinID) const
    {
        std::vector<const Link *> result;
        const Pin *pin = GetPin(pinID);
        if (pin)
        {
            for (const VSE_ID &linkID : pin->ConnectedLinkIDs)
            {
                const Link *link = GetLink(linkID);
                if (link)
                    result.push_back(link);
            }
        }
        return result;
    }

    std::vector<const Link *> Graph::GetLinksConnectedToNode(const VSE_ID &nodeID) const
    {
        std::vector<const Link *> result;
        const Node *node = GetNode(nodeID);
        if (node)
        {
            std::set<VSE_ID> linkIds; // Use a set to avoid duplicate links if both pins are on this node (unlikely for valid links)
            for (const auto &pin : node->InputPins)
            {
                for (const VSE_ID &linkId : pin.ConnectedLinkIDs)
                    linkIds.insert(linkId);
            }
            for (const auto &pin : node->OutputPins)
            {
                for (const VSE_ID &linkId : pin.ConnectedLinkIDs)
                    linkIds.insert(linkId);
            }
            for (const VSE_ID &linkId : linkIds)
            {
                const Link *link = GetLink(linkId);
                if (link)
                    result.push_back(link);
            }
        }
        return result;
    }

    GraphVariable *Graph::AddVariable(const std::string &name, const TypeInfo *type, Variant initialValue, std::string tooltip, VSE_ID desiredVarID)
    {
        if (!type)
        {
            VSE_CORE_ERROR("Graph::AddVariable: TypeInfo cannot be null for variable '{}'.", name);
            return nullptr;
        }
        // Check for name collision
        for (const auto &var : m_Variables)
        {
            if (var->Name == name)
            {
                VSE_CORE_ERROR("Graph::AddVariable: Variable with name '{}' already exists.", name);
                return nullptr;
            }
        }

        VSE_ID newVarID = desiredVarID.empty() ? IDGenerator::Generate() : std::move(desiredVarID);
        if (m_VariableMap.count(newVarID))
        {
            VSE_CORE_ERROR("Graph::AddVariable: Variable with desired ID '{}' already exists.", newVarID);
            return nullptr;
        }

        auto newVar = std::make_unique<GraphVariable>(newVarID, name, type, std::move(initialValue), std::move(tooltip));
        GraphVariable *rawPtr = newVar.get();

        m_Variables.push_back(std::move(newVar));
        m_VariableMap[rawPtr->ID] = rawPtr;
        VSE_CORE_INFO("Graph '{}': Added Variable '{}' (Type: {}, ID: {}).", m_Name, rawPtr->Name, type->Name, rawPtr->ID);
        return rawPtr;
    }

    bool Graph::RemoveVariable(const VSE_ID &varID)
    {
        auto it = std::find_if(m_Variables.begin(), m_Variables.end(),
                               [&](const auto &varPtr)
                               { return varPtr->ID == varID; });
        if (it == m_Variables.end())
        {
            VSE_CORE_WARN("Graph::RemoveVariable: Variable with ID '{}' not found.", varID);
            return false;
        }
        VSE_CORE_INFO("Graph '{}': Removing Variable '{}' (ID: {}).", m_Name, it->get()->Name, varID);
        m_VariableMap.erase(varID);
        m_Variables.erase(it);
        return true;
    }

    GraphVariable *Graph::GetVariable(const VSE_ID &varID)
    {
        auto it = m_VariableMap.find(varID);
        return (it != m_VariableMap.end()) ? it->second : nullptr;
    }
    const GraphVariable *Graph::GetVariable(const VSE_ID &varID) const
    {
        auto it = m_VariableMap.find(varID);
        return (it != m_VariableMap.end()) ? it->second : nullptr;
    }
    GraphVariable *Graph::GetVariableByName(const std::string &name)
    {
        for (auto &var : m_Variables)
        {
            if (var->Name == name)
                return var.get();
        }
        return nullptr;
    }
    const GraphVariable *Graph::GetVariableByName(const std::string &name) const
    {
        for (const auto &var : m_Variables)
        {
            if (var->Name == name)
                return var.get();
        }
        return nullptr;
    }

    void Graph::RebuildLookups()
    {
        VSE_CORE_INFO("Graph '{}': Rebuilding lookup maps.", m_Name);
        m_NodeMap.clear();
        m_PinMap.clear();
        m_LinkMap.clear();
        m_VariableMap.clear();

        for (const auto &node : m_Nodes)
        {
            m_NodeMap[node->ID] = node.get();
            for (auto &pin : node->InputPins)
            {
                m_PinMap[pin.ID] = &pin;
            }
            for (auto &pin : node->OutputPins)
            {
                m_PinMap[pin.ID] = &pin;
            }
        }
        for (const auto &link : m_Links)
        {
            m_LinkMap[link->ID] = link.get();
        }
        for (const auto &var : m_Variables)
        {
            m_VariableMap[var->ID] = var.get();
        }
    }

    void Graph::ResetNodesTransientState()
    {
        for (auto &node : m_Nodes)
        {
            node->ResetPinsTransientState();
            // Potentially reset node-level transient state if any
        }
        VSE_CORE_TRACE("Graph '{}': All nodes' transient pin states reset.", m_Name);
    }

} // namespace VSE