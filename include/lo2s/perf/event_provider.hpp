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

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <lo2s/perf/event_description.hpp>

namespace lo2s
{
namespace perf
{

class EventProvider
{
public:
    using EventMap = std::unordered_map<std::string, EventDescription>;

    EventProvider();
    EventProvider(const EventProvider&) = delete;
    void operator=(const EventProvider&) = delete;

    static const EventProvider& instance()
    {
        return instance_mutable();
    }

    static const EventDescription& get_event_by_name(const std::string& name);

    static bool has_event(const std::string& name);

    static std::vector<EventDescription> get_predefined_events();
    static std::vector<EventDescription> get_pmu_events();

    static const EventDescription& fallback_metric_leader_event();

    class InvalidEvent : public std::runtime_error
    {
    public:
        InvalidEvent(const std::string& event_description)
        : std::runtime_error(std::string{ "Invalid event: " } + event_description)
        {
        }
    };

private:
    static EventProvider& instance_mutable()
    {
        static EventProvider e;
        return e;
    }

    const EventDescription& cache_event(const std::string& name);

    EventMap event_map_;
};
} // namespace perf
} // namespace lo2s
