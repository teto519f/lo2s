/*
 * This file is part of the lo2s software.
 * Linux OTF2 sampling
 *
 * Copyright (c) 2017,
 *    Technische Universitaet Dresden, Germany
 *
 * lo2s is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * lo2s is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with lo2s.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <variant>

#include <lo2s/config.hpp>
#include <lo2s/perf/counter/counter_provider.hpp>
#include <lo2s/perf/event_provider.hpp>
#include <lo2s/perf/tracepoint/format.hpp>
#include <lo2s/perf/util.hpp>
#include <lo2s/time/time.hpp>

#ifndef USE_HW_BREAKPOINT_COMPAT
extern "C"
{
#include <linux/hw_breakpoint.h>
}
#else
extern "C"
{
#include <sys/types.h>
#include <sys/wait.h>
}
#endif

namespace lo2s
{
namespace perf
{
namespace counter
{
namespace group
{

enum class EventType
{
    USERSPACE,
    GROUP,
    SAMPLING,
    TIME,
    TRACEPOINT,
    SYSCALL
};

class PerfEventInstance;

/**
 * @param type Type of Event to measure
 * @param event Name of Tracepoint event to measure, you can use nullopt if measuring different
 * Event types
 */
class PerfEvent
{
protected:
    struct perf_event_attr attr_ = common_perf_event_attrs();
    EventType type_;
    EventDescription data_storage_;
    double scale_ = 1.0;
    ExecutionScope scope_;

public:
    PerfEvent();

    PerfEvent(EventType type, ExecutionScope scope, bool enable_on_exec,
              std::optional<int> event_id);

    PerfEventInstance open();

    double get_scale() const
    {
        return scale_;
    }

    auto get_scope() const
    {
        return scope_;
    }

    perf_event_attr& get_attr()
    {
        return attr_;
    }

    ~PerfEvent()
    {
    }
};

class PerfEventInstance
{
protected:
    int fd_;
    int other_fd_;
    EventType type_;
    PerfEvent ev_;

public:
    PerfEventInstance(PerfEventInstance&) = delete;
    PerfEventInstance(EventType type, PerfEvent ev);

    template <class T>
    T read()
    {
        uint64_t val;
        if (::read(fd_, &val, sizeof(val)) != sizeof(uint64_t))
        {
            throw std::system_error(errno, std::system_category());
        }

        return (static_cast<T>(val)) * ev_.get_scale();
    }

    ~PerfEventInstance()
    {
        close(fd_);
    }
};

} // namespace group
} // namespace counter
} // namespace perf
} // namespace lo2s
