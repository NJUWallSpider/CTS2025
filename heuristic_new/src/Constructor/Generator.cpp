#include "SolutionConstruct.hpp"

void SolutionConstructor::generate_turnarounds(){
    const std::map<std::string, Crew> crews = data_.getCrews();
    const auto& aircraft_routes = data_.getAircraftRoutes();
    for (const auto& flight : data_.getFlights()) {
        // if (flight_assignments.count(flight.id)) {
        //     continue; // Skip assigned flights
        // }

        if (crew_.qualifications.find(flight.id) == crew_.qualifications.end()) {
            continue; // Skip flights crew is not qualified for
        }

        if (flight.depaAirport != current_airport_) {
            continue;
        }

        if (flight.std < last_task_end_time_) {
            continue;
        }

        Turnaround current_turnaround;
        current_turnaround.flights.push_back(&flight);
        current_turnaround.startTime = flight.std;
        current_turnaround.total_flight_time = std::chrono::minutes(flight.flyTime_mins);

        const Flight* current_flight_in_turnaround = &flight;
        while (true) {
            auto route_it = aircraft_routes.find(current_flight_in_turnaround->aircraftNo);
            if (route_it == aircraft_routes.end()) break;

            auto conn_it = std::find_if(route_it->second.connections.begin(), route_it->second.connections.end(), 
                [&](const AircraftFlightConnection& conn){
                return conn.flight->id == current_flight_in_turnaround->id && conn.next_flight != nullptr;
            });

            if (conn_it == route_it->second.connections.end()) break;

            const Flight* next_flight = conn_it->next_flight;
            if (flight_assignments.count(next_flight->id)) break;
            if (crew_.qualifications.find(next_flight->id) == crew_.qualifications.end()) break;

            auto new_total_flight_time = current_turnaround.total_flight_time + std::chrono::minutes(next_flight->flyTime_mins);
            auto new_duty_time = std::chrono::duration_cast<std::chrono::minutes>(next_flight->sta - current_turnaround.startTime);

            if (new_total_flight_time > MAX_FLY_TIME_PER_DUTY || new_duty_time > MAX_DUTY_TIME_PER_FLIGHT_DUTY) break;
            
            current_turnaround.flights.push_back(next_flight);
            current_turnaround.total_flight_time = new_total_flight_time;
            current_flight_in_turnaround = next_flight;
        }

        current_turnaround.endTime = current_flight_in_turnaround->sta;
        current_turnaround.startAirport = current_turnaround.flights.front()->depaAirport;
        current_turnaround.endAirport = current_turnaround.flights.back()->arriAirport;
        candidates.push_back(current_turnaround);
    }
}