/*
 * Copyright © 2014 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Christopher James Halse Rogers <christopher.halse.rogers@canonical.com>
 */

#include <EventHub.h>

#include "mir/input/input_report.h"

#include <umockdev.h>
#include "mir_test_framework/udev_environment.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace mi = mir::input;

namespace
{
class NullInputReport : public mi::InputReport
{
public:
    NullInputReport() = default;

    void received_event_from_kernel(int64_t /*when*/, int /*type*/, int /*code*/, int /*value*/) override
    {
    }
    void published_key_event(int /*dest_fd*/, uint32_t /*seq_id*/, int64_t /*event_time*/) override
    {
    }
    void published_motion_event(int /*dest_fd*/, uint32_t /*seq_id*/, int64_t /*event_time*/) override
    {
    }
    virtual void received_event_finished_signal(int /*src_fd*/, uint32_t /*seq_id*/) override
    {
    }
};

}

TEST(EventHubTest, ScansOnConstruction)
{
    mir::mir_test_framework::UdevEnvironment env;
    env.add_standard_device("synaptics-touchpad");

    auto hub = new android::EventHub{std::make_shared<NullInputReport>()};

    android::RawEvent buffer[10];
    memset(buffer, 0, sizeof(buffer));
    auto num_events = hub->getEvents(0, buffer, 10);

    EXPECT_EQ(static_cast<size_t>(3), num_events);
    EXPECT_EQ(android::EventHub::DEVICE_ADDED, buffer[0].type);
    EXPECT_EQ(android::VIRTUAL_KEYBOARD_ID, buffer[0].deviceId);
    EXPECT_EQ(android::EventHub::DEVICE_ADDED, buffer[1].type);
    EXPECT_EQ(1, buffer[1].deviceId);
    EXPECT_EQ(android::EventHub::FINISHED_DEVICE_SCAN, buffer[2].type);
}

TEST(EventHubTest, GeneratesDeviceAddedOnHotplug)
{
    mir::mir_test_framework::UdevEnvironment env;

    auto hub = new android::EventHub{std::make_shared<NullInputReport>()};

    android::RawEvent buffer[10];
    memset(buffer, 0, sizeof(buffer));
    auto num_events = hub->getEvents(0, buffer, 10);

    EXPECT_EQ(static_cast<size_t>(2), num_events);
    EXPECT_EQ(android::EventHub::DEVICE_ADDED, buffer[0].type);
    EXPECT_EQ(android::VIRTUAL_KEYBOARD_ID, buffer[0].deviceId);
    EXPECT_EQ(android::EventHub::FINISHED_DEVICE_SCAN, buffer[1].type);

    env.add_standard_device("synaptics-touchpad");

    memset(buffer, 0, sizeof(buffer));
    num_events = hub->getEvents(0, buffer, 10);

    EXPECT_EQ(static_cast<size_t>(2), num_events);
    EXPECT_EQ(android::EventHub::DEVICE_ADDED, buffer[0].type);
    EXPECT_EQ(1, buffer[0].deviceId);
    EXPECT_EQ(android::EventHub::FINISHED_DEVICE_SCAN, buffer[1].type);
}

TEST(EventHubTest, GeneratesDeviceRemovedOnHotunplug)
{
    mir::mir_test_framework::UdevEnvironment env;
    env.add_standard_device("synaptics-touchpad");

    auto hub = new android::EventHub{std::make_shared<NullInputReport>()};

    android::RawEvent buffer[10];
    // Flush out initial events.
    auto num_events = hub->getEvents(0, buffer, 10);

    mir::udev::Enumerator devices{std::make_shared<mir::udev::Context>()};
    devices.scan_devices();

    for (auto& device : devices)
    {
        if (device.devnode() && (strcmp(device.devnode(), "/dev/input/event12") == 0))
        {
            env.remove_device((std::string("/sys") + device.devpath()).c_str());
        }
    }

    memset(buffer, 0, sizeof(buffer));
    num_events = hub->getEvents(0, buffer, 10);

    EXPECT_EQ(static_cast<size_t>(2), num_events);
    EXPECT_EQ(android::EventHub::DEVICE_REMOVED, buffer[0].type);
    EXPECT_EQ(1, buffer[0].deviceId);
    EXPECT_EQ(android::EventHub::FINISHED_DEVICE_SCAN, buffer[1].type);

}
