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

#include <lo2s/perf/counter/group/reader.hpp>
#include <lo2s/perf/counter/group/writer.hpp>
#include <lo2s/perf/reader.hpp>

#include <lo2s/build_config.hpp>
#include <lo2s/config.hpp>

#include <lo2s/perf/event_description.hpp>
#include <lo2s/perf/event_provider.hpp>
#include <lo2s/perf/util.hpp>

#include <cstring>

extern "C"
{
#include <sys/ioctl.h>

#include <linux/perf_event.h>
}

namespace lo2s
{
namespace perf
{
namespace counter
{
namespace group
{

template <class T>
Reader<T>::Reader(ExecutionScope scope, bool enable_on_exec)
: counter_collection_(
      CounterProvider::instance().collection_for(MeasurementScope::group_metric(scope))),
  counter_buffer_(counter_collection_.counters.size() + 1)
{
    PerfEvent leader_event(EventType::GROUP, enable_on_exec, counter_collection_.leader,
                           std::nullopt);

    if (scope.is_cpu())
    {
        counter_leader_ = leader_event.open(scope.as_cpu());
    }
    else
    {
        counter_leader_ = leader_event.open(scope.as_thread());
    }

    Log::debug() << "counter::Reader: leader event: '" << counter_collection_.leader.name << "'";

    for (auto& description : counter_collection_.counters)
    {
        if (description.is_supported_in(scope))
        {
            try
            {
                PerfEvent counter_ev(EventType::GROUP, enable_on_exec, description, std::nullopt);

                if (scope.is_cpu())
                {
                    counters_.emplace_back(counter_ev.open(scope.as_cpu()));
                }
                else
                {
                    counters_.emplace_back(counter_ev.open(scope.as_thread()));
                }
            }
            catch (const std::system_error& e)
            {
                Log::error() << "failed to add counter '" << description.name
                             << "': " << e.code().message();

                if (e.code().value() == EINVAL)
                {
                    Log::error()
                        << "opening " << counter_collection_.counters.size()
                        << " counters at once might exceed the hardware limit of simultaneously "
                           "openable counters.";
                }
                throw e;
            }
        }
    }

    if (!enable_on_exec)
    {
        counter_leader_.enable();
    }
    EventReader<T>::init_mmap(counter_leader_.get_fd());
}
template class Reader<Writer>;
} // namespace group
} // namespace counter
} // namespace perf
} // namespace lo2s
