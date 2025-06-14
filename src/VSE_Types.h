#pragma once
#include "VSE_ID.h"     // For VSE_ID if types themselves need IDs (optional for TypeInfo)
#include "VSE_Logger.h" // For logging errors
#include <any>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <memory>
#include <functional> // For std::function (conversions)
#include <stdexcept>  // For exceptions
#include <map>        // For TypeRegistry::m_Conversions

namespace VSE
{
    // Forward declarations
    class Variant;
    class TypeRegistry; // Forward declare TypeRegistry for Variant and TypeInfo

    // Execution token remains the same
    struct ExecToken
    {
    };

    enum class EvaluationState // Corrected typo
    {
        NotEvaluated,
        Evaluating,
        Evaluated
    };

    enum class PinType
    {
        Execution,
        Data
    };

    enum class PinDirection
    {
        Input,
        Output
    };

    struct TypeInfo
    {
        std::type_index Index;
        std::string Name; // User-friendly name (e.g., "Integer", "String")

        TypeInfo(std::type_index index, std::string name)
            : Index(index), Name(std::move(name)) {}

        bool IsSame(const TypeInfo &other) const
        {
            return Index == other.Index;
        }

        // Declaration only; definition after TypeRegistry is fully defined
        bool IsAssignableFrom(const TypeInfo &other, const TypeRegistry &registry) const;
    };

    class Variant
    {
    public:
        Variant() : m_TypeInfo(nullptr) {}

        template <typename T>
        Variant(T value) : m_TypeInfo(nullptr)
        {
            // Assign will be defined after TypeRegistry
            Assign(std::forward<T>(value));
        }

        // Copy constructor
        Variant(const Variant &other);
        // Move constructor
        Variant(Variant &&other) noexcept;

        // Copy assignment
        Variant &operator=(const Variant &other);
        // Move assignment
        Variant &operator=(Variant &&other) noexcept;

        // Definitions of these templated getters will be moved after TypeRegistry
        template <typename T>
        T &Get() &;
        template <typename T>
        T &&Get() &&;
        template <typename T>
        const T &Get() const &;
        template <typename T>
        const T &&Get() const &&;
        template <typename T>
        T GetValue() const;
        template <typename T>
        T *TryGet();
        template <typename T>
        const T *TryGet() const;

        const TypeInfo *GetTypeInfo() const { return m_TypeInfo; }
        bool HasValue() const { return m_Data.has_value() && m_TypeInfo != nullptr; }
        void Reset()
        {
            m_Data.reset();
            m_TypeInfo = nullptr;
        }

        // Declaration only; definition after TypeRegistry is fully defined
        Variant ConvertTo(const TypeInfo &targetType, const TypeRegistry &registry) const;

    private:
        // Declaration only; definition after TypeRegistry is fully defined
        template <typename T>
        void Assign(T &&value);
        // Declaration only; definition after TypeRegistry is fully defined
        template <typename T>
        void ValidateType() const;

        std::any m_Data;
        const TypeInfo *m_TypeInfo; // Pointer to TypeInfo in TypeRegistry (owned by TypeRegistry)
    };

    class TypeRegistry
    {
    public:
        using ConversionFunction = std::function<Variant(const Variant &, const TypeRegistry &)>;

        static TypeRegistry &Instance()
        {
            static TypeRegistry instance;
            return instance;
        }

        template <typename T>
        static const TypeInfo *RegisterType(const std::string &name)
        {
            std::type_index index = typeid(std::decay_t<T>);
            auto &instance = Instance();
            auto it = instance.m_Types.find(index);
            if (it == instance.m_Types.end())
            {
                VSE_CORE_INFO("Registering type: {} (Internal: {})", name, index.name());
                auto result = instance.m_Types.emplace(
                    std::piecewise_construct,
                    std::forward_as_tuple(index),
                    std::forward_as_tuple(index, name));
                return &result.first->second;
            }
            VSE_CORE_WARN("Type already registered: {} (Internal: {}). Returning existing.", name, index.name());
            return &it->second;
        }

        template <typename T>
        static const TypeInfo *Get()
        {
            // Definition of GetTypeInfoForType<T> is below
            return Instance().GetTypeInfoForType<T>();
        }

        static const TypeInfo *GetByName(const std::string &name)
        {
            // Definition of GetTypeInfoByName is below
            return Instance().GetTypeInfoByName(name);
        }

        static const TypeInfo *GetByIndex(const std::type_index &index)
        {
            // Definition of GetTypeInfoByIndex is below
            return Instance().GetTypeInfoByIndex(index);
        }

        template <typename T>
        static std::string GetBaseName()
        {
            return typeid(T).name();
        }

        static void RegisterConversion(const TypeInfo *fromType, const TypeInfo *toType, ConversionFunction converter);
        bool CanConvert(const TypeInfo *fromType, const TypeInfo *toType) const;
        Variant Convert(const Variant &sourceVariant, const TypeInfo *targetType) const;

    private:
        TypeRegistry() { VSE_CORE_INFO("TypeRegistry Initialized."); }
        ~TypeRegistry() { VSE_CORE_INFO("TypeRegistry Destroyed."); }
        TypeRegistry(const TypeRegistry &) = delete;
        TypeRegistry &operator=(const TypeRegistry &) = delete;

        // Definitions of these helpers are placed after the class definition
        template <typename T>
        const TypeInfo *GetTypeInfoForType();
        const TypeInfo *GetTypeInfoByName(const std::string &name) const;
        const TypeInfo *GetTypeInfoByIndex(const std::type_index &index) const;

        std::unordered_map<std::type_index, TypeInfo> m_Types;
        std::map<std::pair<std::type_index, std::type_index>, ConversionFunction> m_Conversions;

        // Friend declarations for implementations that are now outside but logically part of these classes
        friend class Variant; // General friendship for simplicity here
        friend bool TypeInfo::IsAssignableFrom(const TypeInfo &other, const TypeRegistry &registry) const;
    };

    // --- Implementations for TypeRegistry private helpers ---
    template <typename T>
    inline const TypeInfo *TypeRegistry::GetTypeInfoForType()
    {
        auto it = m_Types.find(typeid(std::decay_t<T>));
        if (it == m_Types.end())
        {
            VSE_CORE_ERROR("Type not registered: {}", GetBaseName<T>());
            throw std::runtime_error("Type not registered: " + GetBaseName<T>());
        }
        return &it->second;
    }

    inline const TypeInfo *TypeRegistry::GetTypeInfoByName(const std::string &name) const
    {
        for (const auto &pair : m_Types)
        {
            if (pair.second.Name == name)
            {
                return &pair.second;
            }
        }
        VSE_CORE_WARN("TypeInfo not found by name: {}", name);
        return nullptr;
    }

    inline const TypeInfo *TypeRegistry::GetTypeInfoByIndex(const std::type_index &index) const
    {
        auto it = m_Types.find(index);
        if (it == m_Types.end())
        {
            VSE_CORE_WARN("TypeInfo not found by index: {}", index.name());
            return nullptr;
        }
        return &it->second;
    }

    // --- Implementations for TypeRegistry public conversion methods ---
    inline void TypeRegistry::RegisterConversion(const TypeInfo *fromType, const TypeInfo *toType, ConversionFunction converter)
    {
        if (!fromType || !toType)
        {
            VSE_CORE_ERROR("RegisterConversion: fromType or toType is null.");
            return;
        }
        VSE_CORE_INFO("Registering conversion from {} to {}", fromType->Name, toType->Name);
        Instance().m_Conversions[{fromType->Index, toType->Index}] = std::move(converter);
    }

    inline bool TypeRegistry::CanConvert(const TypeInfo *fromType, const TypeInfo *toType) const
    {
        if (!fromType || !toType)
            return false;
        if (fromType->IsSame(*toType))
            return true;
        return m_Conversions.count({fromType->Index, toType->Index});
    }

    inline Variant TypeRegistry::Convert(const Variant &sourceVariant, const TypeInfo *targetType) const
    {
        if (!sourceVariant.HasValue() || !targetType)
        {
            VSE_CORE_WARN("Convert: Invalid arguments (null source, null targetType, or empty source variant).");
            return {};
        }
        const TypeInfo *sourceType = sourceVariant.GetTypeInfo();
        if (sourceType->IsSame(*targetType))
        {
            return sourceVariant;
        }
        auto it = m_Conversions.find({sourceType->Index, targetType->Index});
        if (it != m_Conversions.end())
        {
            try
            {
                return it->second(sourceVariant, *this);
            }
            catch (const std::exception &e)
            {
                VSE_CORE_ERROR("Conversion failed from {} to {}: {}", sourceType->Name, targetType->Name, e.what());
                return {};
            }
        }
        VSE_CORE_WARN("No conversion registered from {} to {}", sourceType->Name, targetType->Name);
        return {};
    }

    // --- Implementations for Variant methods that depend on TypeRegistry ---
    template <typename T>
    inline void Variant::Assign(T &&value)
    {
        using PureType = std::decay_t<T>;
        m_TypeInfo = TypeRegistry::Instance().GetTypeInfoForType<PureType>(); // Now TypeRegistry is fully defined
        m_Data = std::forward<T>(value);
    }

    template <typename T>
    inline void Variant::ValidateType() const
    {
        if (!m_Data.has_value() || !m_TypeInfo)
        {
            VSE_CORE_ERROR("Variant::ValidateType - Variant has no value or TypeInfo.");
            throw std::bad_any_cast();
        }
        if (m_TypeInfo->Index != typeid(std::decay_t<T>))
        {
            // Now TypeRegistry::GetBaseName<T>() is accessible as TypeRegistry is fully defined
            VSE_CORE_ERROR("Variant::ValidateType - Type mismatch. Expected: {}, Got: {}.",
                           TypeRegistry::GetBaseName<T>(), m_TypeInfo->Name);
            throw std::bad_any_cast();
        }
    }

    // --- Implementations for Variant Getters ---
    template <typename T>
    inline T &Variant::Get() &
    {
        ValidateType<T>();
        return std::any_cast<T &>(m_Data);
    }
    template <typename T>
    inline T &&Variant::Get() &&
    {
        ValidateType<T>();
        return std::any_cast<T &&>(std::move(m_Data));
    }
    template <typename T>
    inline const T &Variant::Get() const &
    {
        ValidateType<T>();
        return std::any_cast<const T &>(m_Data);
    }
    template <typename T>
    inline const T &&Variant::Get() const &&
    {
        ValidateType<T>();
        return std::any_cast<const T &&>(std::move(m_Data));
    } // Matched previous version
    template <typename T>
    inline T Variant::GetValue() const
    {
        ValidateType<T>();
        return std::any_cast<T>(m_Data);
    }

    template <typename T>
    inline T *Variant::TryGet()
    {
        if (!m_Data.has_value() || !m_TypeInfo || m_TypeInfo->Index != typeid(std::decay_t<T>))
            return nullptr;
        return std::any_cast<T>(&m_Data);
    }
    template <typename T>
    inline const T *Variant::TryGet() const
    {
        if (!m_Data.has_value() || !m_TypeInfo || m_TypeInfo->Index != typeid(std::decay_t<T>))
            return nullptr;
        return std::any_cast<const T>(&m_Data);
    }

    // --- Implementations for Variant copy/move constructors and assignments ---
    inline Variant::Variant(const Variant &other)
        : m_Data(other.m_Data), m_TypeInfo(other.m_TypeInfo) {}

    inline Variant::Variant(Variant &&other) noexcept
        : m_Data(std::move(other.m_Data)), m_TypeInfo(other.m_TypeInfo)
    {
        other.m_TypeInfo = nullptr;
    }

    inline Variant &Variant::operator=(const Variant &other)
    {
        if (this != &other)
        {
            m_Data = other.m_Data;
            m_TypeInfo = other.m_TypeInfo;
        }
        return *this;
    }

    inline Variant &Variant::operator=(Variant &&other) noexcept
    {
        if (this != &other)
        {
            m_Data = std::move(other.m_Data);
            m_TypeInfo = other.m_TypeInfo;
            other.m_TypeInfo = nullptr;
        }
        return *this;
    }

    inline Variant Variant::ConvertTo(const TypeInfo &targetType, const TypeRegistry &registry) const
    {
        if (!HasValue())
        {
            VSE_CORE_WARN("Attempted to convert an empty Variant.");
            return {};
        }
        if (m_TypeInfo->IsSame(targetType))
        {
            return *this;
        }
        // registry.Convert will handle the actual conversion logic
        return registry.Convert(*this, &targetType);
    }

    // --- Implementation for TypeInfo::IsAssignableFrom ---
    inline bool TypeInfo::IsAssignableFrom(const TypeInfo &sourceCandidateType, const TypeRegistry &registry) const
    {
        if (this->IsSame(sourceCandidateType))
        {
            return true;
        }
        // The 'registry' argument is used here to call CanConvert
        return registry.CanConvert(&sourceCandidateType, this);
    }

} // namespace VSE