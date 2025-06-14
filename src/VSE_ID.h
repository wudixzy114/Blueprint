#pragma once
#include <string>
#include "uuid.h"

// Forward declare to avoid including spdlog in this low-level header if possible
// Or include VSE_Logger.h if it's lightweight and needed for logging in IDGenerator
// #include "VSE_Logger.h"

namespace VSE
{
    // Using stduuid::uuid
    using VSE_ID_Data = uuids::uuid; // Note the namespace 'uuids' from stduuid

    // VSE_ID will primarily be the string representation.
    using VSE_ID = std::string;

    namespace IDGenerator
    {
        inline VSE_ID_Data GenerateUUID()
        {
            // stduuid provides a default random generator
            return uuids::uuid_system_generator{}();
        }

        inline VSE_ID Generate()
        {
            return uuids::to_string(GenerateUUID());
        }

        inline std::optional<VSE_ID_Data> FromString(const std::string &s)
        {
            // stduuid can throw an exception on invalid input.
            // It also returns std::optional<uuid> from string_view constructor
            // or you can use a try-catch for the direct constructor.
            try
            {
                // Create from string_view for optional behavior
                // Or directly: return uuids::uuid::from_string(s); if that exists and you prefer its error handling.
                // The uuids::uuid(string_view) constructor returns optional.
                // However, direct uuids::uuid(const std_string&) might throw.
                // Let's use a robust way.
                auto id_opt = uuids::uuid::from_string(s); // Check stduuid docs for exact API
                                                           // from_string typically returns optional
                if (id_opt.has_value())
                {
                    return id_opt.value();
                }
                return std::nullopt;
            }
            catch (const std::exception & /*e*/) // Catch potential exceptions from parsing
            {
                // VSE_CORE_WARN("Failed to parse UUID string '{}': {}", s, e.what()); // If logging is available
                return std::nullopt;
            }
        }

        inline bool IsNil(const VSE_ID_Data &id)
        {
            return id.is_nil();
        }

        inline bool IsNilString(const VSE_ID &id_str)
        {
            auto opt_uuid = FromString(id_str);
            // If parsing fails, FromString returns nullopt.
            // If it's a valid nil UUID, id.is_nil() will be true.
            return opt_uuid ? opt_uuid->is_nil() : true; // Treat invalid/unparsable string as effectively nil for checks
        }

    } // namespace IDGenerator

} // namespace VSE

// int main() {
//     VSE::VSE_ID new_id_str = VSE::IDGenerator::Generate();
//     std::cout << "Generated stduuid (string): " << new_id_str << std::endl;

//     std::optional<VSE::VSE_ID_Data> id_data_opt = VSE::IDGenerator::FromString(new_id_str);
//     if (id_data_opt) {
//         std::cout << "Parsed VSE_ID_Data is nil: " << std::boolalpha << id_data_opt->is_nil() << std::endl;
//         // You can use id_data_opt.value() or *id_data_opt here
//     }

//     VSE::VSE_ID_Data another_uuid = VSE::IDGenerator::GenerateUUID();
//     // ... use another_uuid (which is uuids::uuid) ...
//     return 0;
// }