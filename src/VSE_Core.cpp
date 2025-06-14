#include "VSE_Types.h"
#include "VSE_Logger.h"

namespace VSE
{
    void InitializeCoreTypes()
    {
        VSE_CORE_INFO("Initializing VSE Core Types...");
        // Register fundamental types
        TypeRegistry::RegisterType<void>("Void"); // For functions that return void
        TypeRegistry::RegisterType<ExecToken>("Exec");
        TypeRegistry::RegisterType<int>("Integer");
        TypeRegistry::RegisterType<float>("Float");
        TypeRegistry::RegisterType<double>("Double");
        TypeRegistry::RegisterType<bool>("Boolean");
        TypeRegistry::RegisterType<std::string>("String");
        // Add any other fundamental C++ types you'll use directly in nodes

        // Register common conversions
        const auto *intType = TypeRegistry::Get<int>();
        const auto *floatType = TypeRegistry::Get<float>();
        const auto *doubleType = TypeRegistry::Get<double>();
        const auto *boolType = TypeRegistry::Get<bool>();
        const auto *stringType = TypeRegistry::Get<std::string>();

        if (intType && floatType)
        {
            TypeRegistry::RegisterConversion(intType, floatType,
                                             [](const Variant &src, const TypeRegistry & /*reg*/) -> Variant
                                             {
                                                 VSE_CORE_TRACE("Converting int to float");
                                                 return Variant(static_cast<float>(src.GetValue<int>()));
                                             });
        }
        if (floatType && intType)
        {
            TypeRegistry::RegisterConversion(floatType, intType,
                                             [](const Variant &src, const TypeRegistry & /*reg*/) -> Variant
                                             {
                                                 VSE_CORE_TRACE("Converting float to int (truncation)");
                                                 return Variant(static_cast<int>(src.GetValue<float>()));
                                             });
        }
        if (intType && doubleType)
        {
            TypeRegistry::RegisterConversion(intType, doubleType,
                                             [](const Variant &src, const TypeRegistry & /*reg*/) -> Variant
                                             {
                                                 VSE_CORE_TRACE("Converting int to double");
                                                 return Variant(static_cast<double>(src.GetValue<int>()));
                                             });
        }
        // ... add more: float <-> double, int/float/bool -> string etc.
        if (intType && stringType)
        {
            TypeRegistry::RegisterConversion(intType, stringType,
                                             [](const Variant &src, const TypeRegistry & /*reg*/) -> Variant
                                             {
                                                 VSE_CORE_TRACE("Converting int to string");
                                                 return Variant(std::to_string(src.GetValue<int>()));
                                             });
        }
        if (floatType && stringType)
        {
            TypeRegistry::RegisterConversion(floatType, stringType,
                                             [](const Variant &src, const TypeRegistry & /*reg*/) -> Variant
                                             {
                                                 VSE_CORE_TRACE("Converting float to string");
                                                 return Variant(std::to_string(src.GetValue<float>()));
                                             });
        }
        if (boolType && stringType)
        {
            TypeRegistry::RegisterConversion(boolType, stringType,
                                             [](const Variant &src, const TypeRegistry & /*reg*/) -> Variant
                                             {
                                                 VSE_CORE_TRACE("Converting bool to string");
                                                 return Variant(src.GetValue<bool>() ? std::string("true") : std::string("false"));
                                             });
        }

        VSE_CORE_INFO("VSE Core Types Initialized.");
    }
}