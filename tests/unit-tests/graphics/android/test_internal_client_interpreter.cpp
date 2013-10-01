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
 * Authored by: Kevin DuBois <kevin.dubois@canonical.com>
 */

#include "src/server/graphics/android/internal_client_window.h"
#include "mir_test_doubles/mock_buffer.h"
#include "mir_test_doubles/mock_interpreter_resource_cache.h"
#include "mir_test_doubles/mock_fence.h"
#include "mir/graphics/internal_surface.h"

#include <gtest/gtest.h>
#include <stdexcept>

namespace mg=mir::graphics;
namespace mtd=mir::test::doubles;
namespace mga=mir::graphics::android;
namespace geom=mir::geometry;

namespace
{
class MockInternalSurface : public mg::InternalSurface
{
public:
    MOCK_CONST_METHOD0(size, geom::Size());
    MOCK_CONST_METHOD0(pixel_format, MirPixelFormat());
    MOCK_METHOD0(advance_client_buffer, std::shared_ptr<mg::Buffer>());
};

struct InternalClientWindow : public ::testing::Test
{
    void SetUp()
    {
        using namespace testing;
        sz = geom::Size{4, 23};
        mock_cache = std::make_shared<mtd::MockInterpreterResourceCache>();
        mock_surface = std::make_shared<MockInternalSurface>();
        mock_buffer = std::make_shared<mtd::MockBuffer>();
        stub_anw = std::make_shared<ANativeWindowBuffer>();

        ON_CALL(*mock_surface, advance_client_buffer())
            .WillByDefault(Return(mock_buffer));
        ON_CALL(*mock_surface, pixel_format())
            .WillByDefault(Return(mir_pixel_format_abgr_8888));
        ON_CALL(*mock_buffer, native_buffer_handle())
            .WillByDefault(Return(stub_anw));
    }

    std::shared_ptr<ANativeWindowBuffer> stub_anw;
    std::shared_ptr<mtd::MockInterpreterResourceCache> mock_cache;
    std::shared_ptr<MockInternalSurface> mock_surface;
    std::shared_ptr<mtd::MockBuffer> mock_buffer;
    geom::Size sz;
};

}

TEST_F(InternalClientWindow, driver_requests_buffer)
{
    using namespace testing;
    EXPECT_CALL(*mock_surface, advance_client_buffer())
        .Times(1);
    EXPECT_CALL(*mock_buffer, native_buffer_handle())
        .Times(1);
    std::shared_ptr<mg::Buffer> tmp = mock_buffer;
    EXPECT_CALL(*mock_cache, store_buffer(tmp, stub_anw.get()))
        .Times(1);

    mga::InternalClientWindow interpreter(mock_surface, mock_cache);
    auto test_buffer = interpreter.driver_requests_buffer();
    EXPECT_EQ(stub_anw.get(), test_buffer); 
}

TEST_F(InternalClientWindow, driver_returns_buffer)
{
    using namespace testing;
    auto mock_sync = std::make_shared<mtd::MockFence>();

    Sequence seq;
    EXPECT_CALL(*mock_sync, wait())
        .InSequence(seq);
    EXPECT_CALL(*mock_cache, retrieve_buffer(stub_anw.get()))
        .InSequence(seq)
        .WillOnce(Return(mock_buffer));

    mga::InternalClientWindow interpreter(mock_surface, mock_cache);
    auto test_bufferptr = interpreter.driver_requests_buffer();
    interpreter.driver_returns_buffer(test_bufferptr, mock_sync);
}

TEST_F(InternalClientWindow, size_test)
{
    using namespace testing;
    EXPECT_CALL(*mock_surface, size())
        .Times(2)
        .WillOnce(Return(sz)) 
        .WillOnce(Return(sz)); 
    mga::InternalClientWindow interpreter(mock_surface, mock_cache);

    unsigned int rc_width = interpreter.driver_requests_info(NATIVE_WINDOW_WIDTH);
    unsigned int rc_height = interpreter.driver_requests_info(NATIVE_WINDOW_HEIGHT);

    EXPECT_EQ(sz.width.as_uint32_t(), rc_width); 
    EXPECT_EQ(sz.height.as_uint32_t(), rc_height); 
}

TEST_F(InternalClientWindow, driver_default_format)
{
    using namespace testing;
    EXPECT_CALL(*mock_surface, pixel_format())
        .Times(1);

    mga::InternalClientWindow interpreter(mock_surface, mock_cache);

    auto rc_format = interpreter.driver_requests_info(NATIVE_WINDOW_FORMAT);
    EXPECT_EQ(HAL_PIXEL_FORMAT_RGBA_8888, rc_format); 
}

TEST_F(InternalClientWindow, driver_sets_format)
{
    using namespace testing;
    EXPECT_CALL(*mock_surface, pixel_format())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(mir_pixel_format_abgr_8888));
    mga::InternalClientWindow interpreter(mock_surface, mock_cache);

    interpreter.dispatch_driver_request_format(HAL_PIXEL_FORMAT_RGBA_8888);
    auto rc_format = interpreter.driver_requests_info(NATIVE_WINDOW_FORMAT);
    EXPECT_EQ(HAL_PIXEL_FORMAT_RGBA_8888, rc_format); 
}

TEST_F(InternalClientWindow, window_holds_buffer_resource_for_driver)
{
    mga::InternalClientWindow interpreter(mock_surface, mock_cache);

}
