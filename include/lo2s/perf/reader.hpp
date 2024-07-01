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
#include <lo2s/error.hpp>
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
    struct perf_event_attr attr_;
    EventType type_;
    EventDescription data_storage_;
    double scale_ = 1.0;

public:
    PerfEvent(EventType type, bool enable_on_exec, std::optional<EventDescription> desc,
              std::optional<int> event_id);
    PerfEvent();

    PerfEventInstance open(std::variant<Cpu, Thread> location);

    bool degrade_percision();

    void set_bp_addr(uint64_t local_time);

    double get_scale() const
    {
        return scale_;
    }

    perf_event_attr& get_attr()
    {
        return attr_;
    }

    ~PerfEvent()
    {
    }
};

/**
 * Contains an opened instance of PerfEvent.
 * Use PerfEvent.open() to construct an object
 */
class PerfEventInstance
{
protected:
    int fd_;
    EventType type_;
    PerfEvent ev_;

public:
    PerfEventInstance();
    PerfEventInstance(const EventType& type, PerfEvent& ev, std::variant<Cpu, Thread> location);

    PerfEventInstance(PerfEventInstance&) = delete;
    PerfEventInstance& operator=(const PerfEventInstance&) = delete;

    PerfEventInstance(PerfEventInstance&& other)
    {
        std::swap(fd_, other.fd_);
        std::swap(ev_, other.ev_);
    }

    PerfEventInstance& operator=(PerfEventInstance&& other)
    {
        std::swap(fd_, other.fd_);
        std::swap(ev_, other.ev_);
        return *this;
    }

    void enable();
    void disable();

    void set_output(const PerfEventInstance& other_ev);
    void set_syscall_filter();

    int get_fd() const
    {
        return fd_;
    }

    bool is_valid() const
    {
        return fd_ >= 0;
    };

    template <class T>
    T read()
    {
        T val;
        if (::read(fd_, &val, sizeof(val)) != sizeof(T))
        {
            throw std::system_error(errno, std::system_category());
        }

        return val;
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
