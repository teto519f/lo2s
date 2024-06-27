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

#include <cstdint>

#include <lo2s/perf/reader.hpp>

extern "C"
{
#include <sys/ioctl.h>
}

namespace lo2s
{
namespace perf
{
namespace counter
{
namespace group
{

// helper for visit function
template <class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

// configures perf_event_attr based on desc
void PerfEvent::setup(bool enable_on_exec, std::optional<int> event_id)
{
    // if data_storage_ wasn't set it will contain the following values if not specified otherwise
    // later on:
    attr_.type = data_storage_.type;       //-1
    attr_.config = data_storage_.config;   // 0
    attr_.config1 = data_storage_.config1; // 0

    attr_.exclude_kernel = config().exclude_kernel;
    attr_.sample_period = 1;
    attr_.freq = config().metric_use_frequency;
    attr_.enable_on_exec = enable_on_exec;

    // Event type specific attributes
    switch (type_)
    {
    case EventType::USERSPACE:
    {
        attr_.sample_period = 0;
        // Needed when scaling multiplexed events, and recognize activation phases
        attr_.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;

#ifndef USE_HW_BREAKPOINT_COMPAT
        attr_.use_clockid = config().use_clockid;
        attr_.clockid = config().clockid;
#endif
        break;
    }

    case EventType::GROUP:
    {
        attr_.sample_type = PERF_SAMPLE_TIME | PERF_SAMPLE_READ;
        attr_.freq = config().metric_use_frequency;

        // TODO: check if comments will/should appear multiple times
        if (attr_.freq)
        {
            Log::debug() << "counter::Reader: sample_freq: " << config().metric_frequency;
            attr_.sample_freq = config().metric_frequency;
        }
        else
        {
            Log::debug() << "counter::Reader: sample_period: " << config().metric_count;
            attr_.sample_period = config().metric_count;
        }

        attr_.read_format =
            PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING | PERF_FORMAT_GROUP;

        break;

    case EventType::TIME:
        otf2::chrono::time_point local_time = otf2::chrono::genesis();

        attr_.sample_type = PERF_SAMPLE_TIME;
        attr_.exclude_kernel = 1;

#ifndef USE_HW_BREAKPOINT_COMPAT
        attr_.type = PERF_TYPE_BREAKPOINT;
        attr_.bp_type = HW_BREAKPOINT_W;
        attr_.bp_addr = (uint64_t)(&local_time);
        attr_.bp_len = HW_BREAKPOINT_LEN_8;
        attr_.wakeup_events = 1;
#else
        attr_.type = PERF_TYPE_HARDWARE;
        attr_.config = PERF_COUNT_HW_INSTRUCTIONS;
        attr_.sample_period = 100000000;
        attr_.task = 1;
#endif
        break;
    }

    case EventType::SAMPLING:
    {
        if (config().use_pebs)
        {
            attr_.use_clockid = 0;
        }

        attr_.sample_period = config().sampling_period; // update

        if (config().sampling)
        {
            Log::debug() << "using sampling event \'" << config().sampling_event
                         << "\', period: " << config().sampling_period;

            attr_.mmap = 1;
        }
        else
        {
            // Set up a dummy event for recording calling context enter/leaves only
            attr_.type = PERF_TYPE_SOFTWARE;
            attr_.config = PERF_COUNT_SW_DUMMY;
        }

        attr_.sample_id_all = 1;
        // Generate PERF_RECORD_COMM events to trace changes to the command
        // name of a task.  This is used to write a meaningful name for any
        // traced thread to the archive.
        attr_.comm = 1;
        attr_.context_switch = 1;

        // TODO see if we can remove remove tid
        attr_.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_CPU;
        if (config().enable_cct)
        {
            attr_.sample_type |= PERF_SAMPLE_CALLCHAIN;
        }

        attr_.precise_ip = 3;
        break;
    }

    case EventType::TRACEPOINT:
    {
        assert(event_id.has_value());

        attr_.type = PERF_TYPE_TRACEPOINT;
        attr_.config = event_id.value();
        attr_.sample_type = PERF_SAMPLE_RAW | PERF_SAMPLE_TIME;
        break;
    }

    case EventType::SYSCALL:
    {
        attr_.type = PERF_TYPE_TRACEPOINT;
        attr_.sample_type = PERF_SAMPLE_RAW | PERF_SAMPLE_TIME | PERF_SAMPLE_IDENTIFIER;
        // TODO: remove EventFormat stuff from here, do both enter & exit
        // attr_.config = tracepoint::EventFormat("raw_syscalls:sys_exit").id();
        attr_.config = tracepoint::EventFormat("raw_syscalls:sys_enter").id();
        break;
    }
    }
}

PerfEvent::PerfEvent(EventType type, bool enable_on_exec, std::optional<EventDescription> desc,
                     std::optional<int> event_id)
: type_(type)
{
    // set data_storage
    if (type_ == EventType::GROUP || type_ == EventType::USERSPACE || type_ == EventType::SAMPLING)
    {
        assert(desc.has_value());
        data_storage_ = desc.value();
    }

    // configure Perf_event_attr
    setup(enable_on_exec, (event_id.has_value()) ? event_id : std::nullopt);
}

PerfEvent::PerfEvent()
{
    memset(&attr_, 0, sizeof(attr_));
    attr_.size = sizeof(attr_);
    attr_.type = -1;
}

// returns opened PerfEvent instance
PerfEventInstance PerfEvent::open(std::variant<Cpu, Thread> location)
{
    return PerfEventInstance(type_, *this, location);
};

PerfEventInstance::PerfEventInstance(){};

PerfEventInstance::PerfEventInstance(EventType type, PerfEvent ev,
                                     std::variant<Cpu, Thread> location)
: type_(type), ev_(ev)
{
    // can be deleted when scope gets replaced
    ExecutionScope scope;
    std::visit(overloaded{ [&](Cpu cpu) { scope = cpu.as_scope(); },
                           [&](Thread thread) { scope = thread.as_scope(); } },
               location);

    if (type_ == EventType::TIME)
    {
        fd_ = perf_event_open(&ev_.get_attr(), scope, -1, 0);

        if (fd_ == -1)
        {
            throw;
        }
    }
    else if (type_ == EventType::USERSPACE || type_ == EventType::GROUP)
    {
        fd_ = perf_try_event_open(&ev_.get_attr(), scope, -1, 0, config().cgroup_fd);

        if (type_ == EventType::GROUP)
        {
            if (fd_ < 0)
            {
                Log::error() << "perf_event_open for counter group leader failed";
                throw_errno();
            }
        }
        if (type_ == EventType::USERSPACE)
        {
            if (fd_ < 0)
            {
                Log::error() << "perf_event_open for counter failed";
                throw_errno();
            }
        }
    }
    else
    {
        fd_ = perf_event_open(&ev_.get_attr(), scope, -1, 0, config().cgroup_fd);

        if (type_ == EventType::SYSCALL || type_ == EventType::TRACEPOINT)
        {

            if (fd_ < 0)
            {
                Log::error() << "perf_event_open for raw tracepoint failed.";
                throw_errno();
            }
        }

        else if (type_ == EventType::SAMPLING)
        {
            if (errno == EACCES && !ev_.get_attr().exclude_kernel && perf_event_paranoid() > 1)
            {
                ev_.get_attr().exclude_kernel = 1;
                perf_warn_paranoid();
            }
            // since this will most likely be called from a while-loop, the error handling can't
            // happen here
        }
    }
}

} // namespace group
} // namespace counter
} // namespace perf
} // namespace lo2s
