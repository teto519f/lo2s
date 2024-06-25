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
private:
    struct perf_event_attr attr_ = common_perf_event_attrs();
    EventType type_;
    EventDescription data_storage_;
    double scale_ = 1.0;
    ExecutionScope scope_;

public:
    PerfEvent(EventType type, ExecutionScope scope, bool enable_on_exec,
              std::optional<std::string> event)
    : type_(type), scope_(scope)
    {
        // set data_storage
        switch (type_)
        {
        case EventType::GROUP:
            data_storage_ =
                (CounterProvider::instance().collection_for(MeasurementScope::group_metric(scope_)))
                    .leader;
            break;
        case EventType::USERSPACE:
            data_storage_ = (CounterProvider::instance().collection_for(
                                 MeasurementScope::userspace_metric(scope_)))
                                .leader;
            break;
        case EventType::SAMPLING:
            data_storage_ = EventProvider::get_event_by_name(config().sampling_event);
        default:
            break;
        }

        // general attributes
        attr_.type = data_storage_.type;
        attr_.config = data_storage_.config;
        attr_.config1 = data_storage_.config1;
        attr_.exclude_kernel = config().exclude_kernel;
        attr_.sample_period = 1;
        attr_.freq = config().metric_use_frequency;
        attr_.enable_on_exec = enable_on_exec;

        // Event type specific attributes
        switch (type_)
        {
        case EventType::USERSPACE:
        {
            // something
            break;
        }

        case EventType::GROUP:
        {
            attr_.sample_type = PERF_SAMPLE_TIME | PERF_SAMPLE_READ;
            attr_.freq = config().metric_use_frequency;

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
            scope_ = ExecutionScope(Thread(0));

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
            attr_.sample_type =
                PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_CPU;
            if (config().enable_cct)
            {
                attr_.sample_type |= PERF_SAMPLE_CALLCHAIN;
            }

            attr_.precise_ip = 3;
            break;
        }

        case EventType::TRACEPOINT:
        {
            if (!event.has_value())
            {
                throw; // do some error magic
            }

            attr_.type = PERF_TYPE_TRACEPOINT;
            attr_.config = tracepoint::EventFormat(event.value()).id();
            attr_.sample_type = PERF_SAMPLE_RAW | PERF_SAMPLE_TIME;
            break;
        }

        case EventType::SYSCALL:
        {
            attr_.type = PERF_TYPE_TRACEPOINT;
            attr_.sample_type = PERF_SAMPLE_RAW | PERF_SAMPLE_TIME | PERF_SAMPLE_IDENTIFIER;
            setSyscallEventFormat(false);
            break;
        }
        }
    }

    void setSyscallEventFormat(bool useExit)
    {
        if (useExit)
        {
            attr_.config = tracepoint::EventFormat("raw_syscalls:sys_exit").id();
        }
        else
        {
            attr_.config = tracepoint::EventFormat("raw_syscalls:sys_enter").id();
        }
    }

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

    PerfEventInstance open(std::variant<Cpu, Thread> location);
};

class PerfEventInstance
{
private:
    int fd_;
    int other_fd_;
    EventType type_;
    PerfEvent ev_;

public:
    PerfEventInstance(EventType type, PerfEvent ev) : type_(type), ev_(ev){};

    template <class T>
    T open(std::variant<Cpu, Thread> location)
    {
        switch (type_)
        {
        case EventType::GROUP:
        {
            fd_ = perf_try_event_open(&ev_.get_attr(), ev_.get_scope(), -1, 0, config().cgroup_fd);

            if (fd_ < 0)
            {
                Log::error() << "perf_event_open for counter group leader failed";
                throw; // TODO: do some template stuff so throw__errno() works
            }
            break;
        }

        case EventType::USERSPACE:
        {
            // Future me problem, userspace is weird
            break;
        }

        case EventType::TIME:
        {
            fd_ = perf_event_open(&ev_.get_attr(), ev_.get_scope(), -1, 0);
            if (fd_ == -1)
            {
                throw;
            }

            break;
        }

        case EventType::SAMPLING:
        {
            do
            {
                fd_ = perf_event_open(&ev_.get_attr(), ev_.get_scope(), -1, 0, config().cgroup_fd);

                if (errno == EACCES && !ev_.get_attr().exclude_kernel && perf_event_paranoid() > 1)
                {
                    ev_.get_attr().exclude_kernel = 1;
                    perf_warn_paranoid();
                    continue;
                }

                /* reduce exactness of IP can help if the kernel does not support really exact
                 * events */
                if (ev_.get_attr().precise_ip == 0)
                    break;
                else
                    ev_.get_attr().precise_ip--;
            } while (fd_ <= 0);

            if (fd_ < 0)
            {
                Log::error() << "perf_event_open for sampling failed";
                if (ev_.get_attr().use_clockid)
                {
                    Log::error() << "maybe the specified clock is unavailable?";
                }
                throw;
            }
            Log::debug() << "Using precise_ip level: " << ev_.get_attr().precise_ip;
            break;
        }

        case EventType::TRACEPOINT:
        {
            fd_ = perf_event_open(&ev_.get_attr(), ExecutionScope(location), -1, 0,
                                  config().cgroup_fd);
            if (fd_ < 0)
            {
                Log::error() << "perf_event_open for raw tracepoint failed.";
                throw;
            }
            break;
        }

        case EventType::SYSCALL:
        {
            fd_ = perf_event_open(&ev_.get_attr(), ExecutionScope(location), -1, 0,
                                  config().cgroup_fd);
            if (fd_ < 0)
            {
                Log::error() << "perf_event_open for raw tracepoint failed.";
                throw;
            }

            ev_.setSyscallEventFormat(true);
            other_fd_ = perf_event_open(&ev_.get_attr(), ExecutionScope(location), -1, 0,
                                        config().cgroup_fd);
            if (other_fd_ < 0)
            {
                Log::error() << "perf_event_open for raw tracepoint failed.";
                throw;
                close(fd_);
            }
            break;
        }
        }
    }

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
