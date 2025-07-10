import pandas as pd
import re
from datetime import datetime, timedelta
from collections import defaultdict

# --- Utility Functions from previous analysis ---

def get_crews_with_ground_duty(ground_duty_path):
    ground_duty_df = pd.read_csv(ground_duty_path)
    return set(ground_duty_df['crewId'].unique())

def get_crew_schedules_and_locations(report_path):
    with open(report_path, 'r', encoding='utf-8') as f:
        content = f.read()
    crew_data = {}
    crew_chunks = content.split('Crew ID: ')
    for chunk in crew_chunks[1:]:
        if not chunk.strip(): continue
        lines = chunk.strip().split('\n')
        crew_id = "Crew_" + lines[0].strip().split('_')[1]
        crew_data[crew_id] = {'tasks': [], 'base': lines[1].replace('Base:', '').strip()}
        for i, line in enumerate(lines):
            task_match = re.search(r'\[(2025-\d{2}-\d{2}\s\d{2}:\d{2})\s*-\s*(2025-\d{2}-\d{2}\s\d{2}:\d{2})\]\s*(Flight|Bus)\s*(\S+).*?\((.*?)\s*->\s*(.*?)\)', line)
            if task_match:
                start_str, end_str, _, _, _, arr = task_match.groups()
                crew_data[crew_id]['tasks'].append({
                    'start': datetime.strptime(start_str.strip(), '%Y-%m-%d %H:%M'),
                    'end': datetime.strptime(end_str.strip(), '%Y-%m-%d %H:%M'),
                    'arrival_location': arr.strip().replace(')', '')
                })
    return crew_data

def get_qualified_crews_for_flight(flight_id, match_file_path):
    qualified_crews = set()
    try:
        for chunk in pd.read_csv(match_file_path, chunksize=200000, usecols=['crewId', 'legId']):
            chunk['crewId'] = chunk['crewId'].str.strip('\'"')
            chunk['legId'] = chunk['legId'].str.strip('\'"')
            qualified_rows = chunk[chunk['legId'] == flight_id]
            if not qualified_rows.empty:
                qualified_crews.update(qualified_rows['crewId'].tolist())
    except Exception as e:
        print(f"Error reading crewLegMatch.csv: {e}")
    return list(qualified_crews)

# --- Main Analysis Function ---

def analyze_bus_positioning(unassigned_path, flight_path, match_path, report_path, ground_duty_path, bus_path, num_samples=20):
    unassigned_df = pd.read_csv(unassigned_path, header=None, names=['flight_id']).head(num_samples)
    flights_df = pd.read_csv(flight_path)
    bus_df = pd.read_csv(bus_path)
    bus_df['td'] = pd.to_datetime(bus_df['td'])
    bus_df['ta'] = pd.to_datetime(bus_df['ta'])

    crew_schedules = get_crew_schedules_and_locations(report_path)
    crews_with_ground_duty = get_crews_with_ground_duty(ground_duty_path)

    print("Analyzing for potential bus positioning opportunities...")
    
    solvable_by_bus = 0
    total_wrong_location = 0

    for _, row in unassigned_df.iterrows():
        flight_id = row['flight_id']
        flight_info = flights_df[flights_df['id'] == flight_id].iloc[0]
        flight_start = datetime.strptime(flight_info['std'], '%Y/%m/%d %H:%M')
        dep_airport = flight_info['depaAirport']

        all_qualified = get_qualified_crews_for_flight(flight_id, match_path)
        qualified_crews = [c for c in all_qualified if c not in crews_with_ground_duty]
        
        if not qualified_crews: continue

        is_wrong_location_case = False
        bus_found_for_this_flight = False

        for crew_id in qualified_crews:
            crew_info = crew_schedules.get(crew_id, {'tasks': [], 'base': 'Unknown'})

            # Determine availability and last known location
            flight_duration_minutes = int(flight_info['flyTime'])
            is_busy = any(flight_start < task['end'] and (flight_start + timedelta(minutes=flight_duration_minutes)) > task['start'] for task in crew_info['tasks'])
            if is_busy: continue

            last_task_time = datetime.min
            crew_location = crew_info['base']
            for task in sorted(crew_info['tasks'], key=lambda x: x['end']):
                if task['end'] < flight_start:
                    last_task_time = task['end']
                    crew_location = task['arrival_location']
            
            if crew_location == dep_airport: continue # This crew is at the correct location

            # This is a "wrong location" case
            is_wrong_location_case = True

            # Can a bus help?
            possible_buses = bus_df[
                (bus_df['depaAirport'] == crew_location) & 
                (bus_df['arriAirport'] == dep_airport) &
                (bus_df['td'] >= last_task_time) & # Bus departs after crew is free
                (bus_df['ta'] <= flight_start)     # Bus arrives before flight departs
            ]

            if not possible_buses.empty:
                bus_found_for_this_flight = True
                break # Found a solution for this flight, move to the next one
        
        if is_wrong_location_case:
            total_wrong_location += 1
            if bus_found_for_this_flight:
                solvable_by_bus += 1

    print("\n======================================================")
    print("      Bus Positioning Analysis Results")
    print("======================================================")
    print(f"Flights analyzed (sample size): {num_samples}")
    print(f"Flights identified as 'Available but Wrong Location': {total_wrong_location}")
    print(f"Of these, flights with a potential bus solution: {solvable_by_bus}")
    if total_wrong_location > 0:
        percentage = (solvable_by_bus / total_wrong_location) * 100
        print(f"Success Rate: {percentage:.2f}%")
    print("======================================================")


if __name__ == "__main__":
    analyze_bus_positioning(
        unassigned_path='unassigned_flights.txt',
        flight_path='data/0703/flight.csv',
        match_path='data/0703/crewLegMatch.csv',
        report_path='heuristic/report/0703/schedule_report.txt',
        ground_duty_path='data/0703/groundDuty.csv',
        bus_path='data/0703/busInfo.csv'
    ) 