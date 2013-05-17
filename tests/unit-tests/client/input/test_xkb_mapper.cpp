/*
 * Copyright © 2013 Canonical Ltd.
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
 * Authored by: Robert Carr <robert.carr@canonical.com>
 */

#include "mir/input/xkb_mapper.h"

#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <linux/input.h>

#include <gtest/gtest.h>

namespace mircv = mir::input::receiver;

namespace
{

static int map_scancode(mircv::XKBMapper &mapper, MirKeyAction action, int scan_code)
{
    MirKeyEvent ev;
    ev.action = action;
    ev.scan_code = scan_code;
    
    mapper.update_state_and_map_event(ev);
    return ev.key_code;
}

}

TEST(XKBMapper, maps_generic_us_english_keys)
{
    mircv::XKBMapper mapper;
    
    EXPECT_EQ(XKB_KEY_4, map_scancode(mapper, mir_key_action_down, KEY_4));
    EXPECT_EQ(XKB_KEY_Shift_L, map_scancode(mapper, mir_key_action_down, KEY_LEFTSHIFT));
    EXPECT_EQ(XKB_KEY_dollar, map_scancode(mapper, mir_key_action_down, KEY_4));
    EXPECT_EQ(XKB_KEY_dollar, map_scancode(mapper, mir_key_action_up, KEY_4));
    EXPECT_EQ(XKB_KEY_Shift_L, map_scancode(mapper, mir_key_action_up, KEY_LEFTSHIFT));
    EXPECT_EQ(XKB_KEY_4, map_scancode(mapper, mir_key_action_down, KEY_4));
}

TEST(XKBMapper, key_action_multiple_does_not_update_modifier_state)
{
    mircv::XKBMapper mapper;

    EXPECT_EQ(XKB_KEY_Shift_L, map_scancode(mapper, mir_key_action_multiple, KEY_LEFTSHIFT));
    EXPECT_EQ(XKB_KEY_4, map_scancode(mapper, mir_key_action_down, KEY_4));
}
