/*
 * Copyright © 2012 Canonical Ltd.
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
 * Authored by: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 */

#include "src/server/graphics/gbm/gbm_platform.h"
#include "src/server/graphics/gbm/gbm_display.h"
#include "src/server/graphics/gbm/gbm_buffer_allocator.h"
#include "mir/graphics/buffer_basic.h"
#include "mir/graphics/buffer_id.h"
#include "mir/graphics/buffer_properties.h"
#include "mir/graphics/buffer_initializer.h"
#include "mir_test_doubles/stub_buffer.h"
#include "mir_test_doubles/null_platform.h"
#include "mir/graphics/null_display_report.h"
#include "src/server/graphics/default_display_configuration_policy.h"

#include "mir_test_framework/testing_server_configuration.h"

#include <thread>
#include <gtest/gtest.h>
#include <stdexcept>

namespace mc = mir::compositor;
namespace geom = mir::geometry;
namespace mg = mir::graphics;
namespace mtf = mir_test_framework;
namespace mtd = mir::test::doubles;

namespace mir
{

class StubBufferThread : public mtd::StubBuffer
{
public:
    StubBufferThread() :
        creation_thread_id{std::this_thread::get_id()}
    {}

    void bind_to_texture()
    {
        /*
         * If we are trying to bind the texture from a different thread from
         * the one used to create the buffer (i.e. a thread in which the
         * display is not supposed to be configured), force an EGL error to
         * make the tests happy.
         */
        if (std::this_thread::get_id() != creation_thread_id)
        {
            eglInitialize(0, 0, 0);
            throw std::runtime_error("Binding to texture failed");
        }
    }

private:
    std::thread::id creation_thread_id;
};

class StubGraphicBufferAllocator : public mg::GraphicBufferAllocator
{
 public:
    std::shared_ptr<mg::Buffer> alloc_buffer(mg::BufferProperties const&)
    {
        return std::shared_ptr<mg::Buffer>(new StubBufferThread());
    }

    std::vector<geom::PixelFormat> supported_pixel_formats()
    {
        return std::vector<geom::PixelFormat>();
    }
};

class StubGraphicPlatform : public mtd::NullPlatform
{
public:
    std::shared_ptr<mg::GraphicBufferAllocator> create_buffer_allocator(
        const std::shared_ptr<mg::BufferInitializer>& /*buffer_initializer*/) override
    {
        return std::make_shared<StubGraphicBufferAllocator>();
    }
};

class GBMBufferIntegration : public ::testing::Test
{
protected:
    virtual void SetUp()
    {
        auto options = mtf::TestingServerConfiguration().the_options();

        if (options->get("tests-use-real-graphics", false))
            platform = mg::create_platform(options, std::make_shared<mg::NullDisplayReport>());
        else
            platform = std::make_shared<StubGraphicPlatform>();

        auto conf_policy = std::make_shared<mg::DefaultDisplayConfigurationPolicy>();
        display = platform->create_display(conf_policy);
        auto buffer_initializer = std::make_shared<mg::NullBufferInitializer>();
        allocator = platform->create_buffer_allocator(buffer_initializer);
        size = geom::Size{100, 100};
        pf = geom::PixelFormat::abgr_8888;
        usage = mg::BufferUsage::hardware;
        buffer_properties = mg::BufferProperties{size, pf, usage};
    }

    std::shared_ptr<mg::Platform> platform;
    std::shared_ptr<mg::Display> display;
    std::shared_ptr<mg::GraphicBufferAllocator> allocator;
    geom::Size size;
    geom::PixelFormat pf;
    mg::BufferUsage usage;
    mg::BufferProperties buffer_properties;
};

struct BufferCreatorThread
{
    BufferCreatorThread(const std::shared_ptr<mg::GraphicBufferAllocator>& allocator,
                        mg::BufferProperties const& buffer_properties)
        : allocator{allocator}, buffer_properties{buffer_properties}
    {
    }

    void operator()()
    {
        using namespace testing;
        buffer = allocator->alloc_buffer(buffer_properties);
    }

    std::shared_ptr<mg::GraphicBufferAllocator> allocator;
    std::shared_ptr<mg::Buffer> buffer;
    mg::BufferProperties buffer_properties;
};

struct BufferDestructorThread
{
    BufferDestructorThread(std::shared_ptr<mg::Buffer> buffer)
        : buffer{std::move(buffer)}
    {
    }

    void operator()()
    {
        using namespace testing;
        buffer.reset();
        ASSERT_EQ(EGL_SUCCESS, eglGetError());
    }

    std::shared_ptr<mg::Buffer> buffer;
};

struct BufferTextureInstantiatorThread
{
    BufferTextureInstantiatorThread(const std::shared_ptr<mg::Buffer>& buffer)
        : buffer(buffer), exception_thrown(false)
    {
    }

    void operator()()
    {
        using namespace testing;

        try
        {
            buffer->bind_to_texture();
        }
        catch(std::runtime_error const&)
        {
            exception_thrown = true;
        }

        ASSERT_NE(EGL_SUCCESS, eglGetError());
    }

    const std::shared_ptr<mg::Buffer>& buffer;
    bool exception_thrown;
};

TEST_F(GBMBufferIntegration, buffer_creation_from_arbitrary_thread_works)
{
    using namespace testing;

    EXPECT_NO_THROW({
        BufferCreatorThread creator(allocator, buffer_properties);
        std::thread t{std::ref(creator)};
        t.join();
        ASSERT_TRUE(creator.buffer.get() != 0);
    });
}

TEST_F(GBMBufferIntegration, buffer_destruction_from_arbitrary_thread_works)
{
    using namespace testing;

    EXPECT_NO_THROW({
        auto buffer = allocator->alloc_buffer(buffer_properties);
        buffer->bind_to_texture();
        ASSERT_EQ(EGL_SUCCESS, eglGetError());

        BufferDestructorThread destructor{std::move(buffer)};
        std::thread t{std::ref(destructor)};
        t.join();
    });
}

TEST_F(GBMBufferIntegration, buffer_lazy_texture_instantiation_from_arbitrary_thread_fails)
{
    using namespace testing;

    EXPECT_NO_THROW({
        auto buffer = allocator->alloc_buffer(buffer_properties);
        BufferTextureInstantiatorThread texture_instantiator{buffer};
        std::thread t{std::ref(texture_instantiator)};
        t.join();
        ASSERT_TRUE(texture_instantiator.exception_thrown);
    });
}
}
