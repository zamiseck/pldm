#pragma once

#include "serialize.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Inventory/Item/PCIeSlot/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using ItemSlot = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::PCIeSlot>;

class PCIeSlot : public ItemSlot
{
  public:
    PCIeSlot() = delete;
    ~PCIeSlot() = default;
    PCIeSlot(const PCIeSlot&) = delete;
    PCIeSlot& operator=(const PCIeSlot&) = delete;
    PCIeSlot(PCIeSlot&&) = default;
    PCIeSlot& operator=(PCIeSlot&&) = default;

    PCIeSlot(sdbusplus::bus::bus& bus, const std::string& objPath) :
        ItemSlot(bus, objPath.c_str()), path(objPath)
    {
        pldm::serialize::Serialize::getSerialize().serialize(path, "PCIeSlot");
    }

    /** Get value of Generation */
    Generations generation() const override;

    /** Set value of Generation */
    Generations generation(Generations value) override;

    /** Get value of Lanes */
    size_t lanes() const override;

    /** Set value of Lanes */
    size_t lanes(size_t value) override;

    /** Get value of SlotType */
    SlotTypes slotType() const override;

    /** Set value of SlotType */
    SlotTypes slotType(SlotTypes value) override;

    /** Get value of HotPluggable */
    bool hotPluggable() const override;

    /** Set value of HotPluggable */
    bool hotPluggable(bool value) override;

  private:
    std::string path;
};

} // namespace dbus
} // namespace pldm
