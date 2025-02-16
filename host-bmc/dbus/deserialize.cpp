#include "deserialize.hpp"

#include "libpldm/pdr.h"

#include "custom_dbus.hpp"
#include "serialize.hpp"

#include <nlohmann/json.hpp>

namespace pldm
{
namespace deserialize
{
namespace fs = std::filesystem;

using Json = nlohmann::json;
using Properties = std::map<std::string, dbus::PropertyValue>;

using callback =
    std::function<void(const std::string& path,
                       std::map<std::string, dbus::PropertyValue> values)>;

std::unordered_map<std::string, callback> ibmDbusHandler{
    {"LocationCode",
     [](const std::string& path, Properties values) {
         if (values.contains("locationCode"))
         {
             pldm::dbus::CustomDBus::getCustomDBus().setLocationCode(
                 path, std::get<std::string>(values.at("locationCode")));
         }
     }},
    {"Associations",
     [](const std::string& path, Properties values) {
         if (values.contains("associations"))
         {
             pldm::dbus::CustomDBus::getCustomDBus().setAssociations(
                 path,
                 std::get<dbus::AssociationsObj>(values.at("associations")));
         }
     }},
    {"Available",
     [](const std::string& path, Properties values) {
         if (values.contains("available"))
         {
             pldm::dbus::CustomDBus::getCustomDBus().setAvailabilityState(
                 path, std::get<bool>(values.at("available")));
         }
     }},
    {"OperationalStatus",
     [](const std::string& path, Properties values) {
         if (values.contains("functional"))
         {
             pldm::dbus::CustomDBus::getCustomDBus().setOperationalStatus(
                 path, std::get<bool>(values.at("functional")));
         }
     }},
    {"InventoryItem",
     [](const std::string& path, Properties values) {
         if (values.contains("present"))
         {
             pldm::dbus::CustomDBus::getCustomDBus().updateItemPresentStatus(
                 path, std::get<bool>(values.at("present")));
         }
     }},
    {"Enable",
     [](const std::string& path, Properties values) {
         if (values.contains("enabled"))
         {
             pldm::dbus::CustomDBus::getCustomDBus().implementObjectEnableIface(
                 path, std::get<bool>(values.at("enabled")));
         }
     }},
    {"ItemChassis",
     [](const std::string& path, Properties /* values */) {
         pldm::dbus::CustomDBus::getCustomDBus().implementChassisInterface(
             path);
     }},
    {"PCIeSlot",
     [](const std::string& path, Properties /* values */) {
         pldm::dbus::CustomDBus::getCustomDBus().implementPCIeSlotInterface(
             path);
     }},
    {"CPUCore",
     [](const std::string& path, Properties values) {
         if (values.contains("microcode"))
         {
             pldm::dbus::CustomDBus::getCustomDBus().setMicrocode(
                 path, std::get<uint32_t>(values.at("microcode")));
         }
         else
         {
             pldm::dbus::CustomDBus::getCustomDBus().implementCpuCoreInterface(
                 path);
         }
     }},
    {"Motherboard",
     [](const std::string& path, Properties /* values */) {
         pldm::dbus::CustomDBus::getCustomDBus().implementMotherboardInterface(
             path);
     }},
    {"PowerSupply",
     [](const std::string& path, Properties /* values */) {
         pldm::dbus::CustomDBus::getCustomDBus().implementPowerSupplyInterface(
             path);
     }},
    {"Fan",
     [](const std::string& path, Properties /* values */) {
         pldm::dbus::CustomDBus::getCustomDBus().implementFanInterface(path);
     }},
    {"Connector",
     [](const std::string& path, Properties /* values */) {
         pldm::dbus::CustomDBus::getCustomDBus().implementConnecterInterface(
             path);
     }},
    {"VRM",
     [](const std::string& path, Properties /* values */) {
         pldm::dbus::CustomDBus::getCustomDBus().implementVRMInterface(path);
     }},
    {"FabricAdapter",
     [](const std::string& path, Properties /* values */) {
         pldm::dbus::CustomDBus::getCustomDBus().implementFabricAdapter(path);
     }},
    {"Board",
     [](const std::string& path, Properties /* values */) {
         pldm::dbus::CustomDBus::getCustomDBus().implementBoard(path);
     }},
    {"Global",
     [](const std::string& path, Properties /* values */) {
         pldm::dbus::CustomDBus::getCustomDBus().implementGlobalInterface(path);
     }},
    {"LicenseEntry",
     [](const std::string& path, Properties values) {
         std::string name{};
         std::string serialno{};
         dbus::LicenseEntryType type;
         dbus::LicenseEntryAuthorizationType authtype;
         uint32_t authdevno{};
         uint64_t exptime{};

         if (values.contains("name"))
         {
             name = std::get<std::string>(values.at("name"));
         }
         if (values.contains("serialNumber"))
         {
             serialno = std::get<std::string>(values.at("serialNumber"));
         }
         if (values.contains("type"))
         {
             type = std::get<dbus::LicenseEntryType>(values.at("type"));
         }
         if (values.contains("authorizationType"))
         {
             authtype = std::get<dbus::LicenseEntryAuthorizationType>(
                 values.at("authorizationType"));
         }
         if (values.contains("expirationTime"))
         {
             exptime = std::get<uint64_t>(values.at("expirationTime"));
         }
         if (values.contains("authDeviceNumber"))
         {
             authdevno = std::get<uint32_t>(values.at("authDeviceNumber"));
         }

         pldm::dbus::CustomDBus::getCustomDBus().implementLicInterfaces(
             path, authdevno, name, serialno, exptime, type, authtype);
     }},
    {"SoftWareVersion", [](const std::string& path, Properties values) {
         std::string version{};

         if (values.contains("version"))
         {
             version = std::get<std::string>(values.at("version"));
         }

         pldm::dbus::CustomDBus::getCustomDBus().setSoftwareVersion(path,
                                                                    version);
     }}};

std::set<uint16_t> getEntityTypes(const fs::path& path)
{
    std::set<uint16_t> entityTypes{};

    if (!fs::exists(path) || fs::is_empty(path))
    {
        std::cerr << "The file does not exist or is empty, FILE_PATH = " << path
                  << std::endl;
        return entityTypes;
    }

    try
    {
        std::ifstream jsonFile(path);
        auto json = Json::parse(jsonFile);

        // define the default JSON as empty
        const std::set<uint16_t> empty{};
        auto objectPaths = json.value("entityTypes", empty);
        std::ranges::transform(objectPaths,
                               std::inserter(entityTypes, entityTypes.begin()),
                               [](const auto& i) { return i; });
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to parse config file, FILE_PATH = " << path
                  << ", ERROR = " << e.what() << std::endl;
    }

    return entityTypes;
}

void restoreDbusObj(HostPDRHandler* hostPDRHandler)
{
    if (hostPDRHandler == nullptr)
    {
        return;
    }

    if (!pldm::serialize::Serialize::getSerialize().deserialize())
    {
        return;
    }

    auto entityTypes = getEntityTypes(DBUS_JSON_FILE);
    auto savedObjs = pldm::serialize::Serialize::getSerialize().getSavedObjs();

    for (auto& [type, objs] : savedObjs)
    {
        if (!entityTypes.contains(type))
        {
            continue;
        }

        std::cout << "Restoring dbus of type : " << type << std::endl;
        // updateObjectPathMaps();
        for (auto& [path, entites] : objs)
        {
            auto& [num, id, obj] = entites;
            pldm_entity node{type, num, id};
            pldm_entity parent{};
            hostPDRHandler->updateObjectPathMaps(
                path,
                init_pldm_entity_node(node, parent, 0, nullptr, nullptr, 0));
            for (auto& [name, propertyValue] : obj)
            {
                if (!ibmDbusHandler.contains(name))
                {
                    std::cerr
                        << "name is not in ibmDbusHandler, name = " << name
                        << std::endl;
                    continue;
                }
                ibmDbusHandler.at(name)(path, propertyValue);
            }
        }
    }
}

} // namespace deserialize
} // namespace pldm
