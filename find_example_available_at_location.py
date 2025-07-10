import pandas as pd
import re
from datetime import datetime
from collections import defaultdict

# --- Utility Functions ---

def get_crews_with_ground_duty(ground_duty_path):
    return set(pd.read_csv(ground_duty_path)['crewId'].unique())

def get_crew_schedules_and_locations(report_path):
    with open(report_path, 'r', encoding='utf-8') as f:
        content = f.read()
    crew_data = {}
    crew_chunks = content.split('Crew ID: ')
    for chunk in crew_chunks[1:]:
        if not chunk.strip(): continue
        lines = chunk.strip().split('\n')
        crew_id = "Crew_" + lines[0].strip().split('_')[1]
        raw_schedule = "Crew ID: " + chunk.strip()
        crew_data[crew_id] = {'tasks': [], 'base': lines[1].replace('Base:', '').strip(), 'raw_schedule': raw_schedule}
        last_arrival_location = crew_data[crew_id]['base']
        for i, line in enumerate(lines):
            task_match = re.search(r'\[(2025-\d{2}-\d{2}\s\d{2}:\d{2})\s*-\s*(2025-\d{2}-\d{2}\s\d{2}:\d{2})\]\s*(Flight|Bus)\s*(\S+).*?\((.*?)\s*->\s*(.*?)\)', line)
            if task_match:
                start_str, end_str, _, _, _, arr = task_match.groups()
                last_arrival_location = arr.strip().replace(')', '')
                crew_data[crew_id]['tasks'].append({
                    'start': datetime.strptime(start_str.strip(), '%Y-%m-%d %H:%M'),
                    'end': datetime.strptime(end_str.strip(), '%Y-%m-%d %H:%M'),
                    'arrival_location': last_arrival_location
                })
            rest_match = re.search(r'\s*--- Rest Period ---', line)
            if rest_match and i + 2 < len(lines):
                location_line = lines[i+2]
                location_match = re.search(r'\s*Location:\s*(\S+)', location_line)
                if location_match:
                    last_arrival_location = location_match.group(1).strip()
        crew_data[crew_id]['final_location'] = last_arrival_location
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

def find_and_display_example(unassigned_path, flight_path, match_path, report_path, ground_duty_path):
    unassigned_df = pd.read_csv(unassigned_path, header=None, names=['flight_id'])
    flights_df = pd.read_csv(flight_path)
    crew_schedules = get_crew_schedules_and_locations(report_path)
    crews_with_ground_duty = get_crews_with_ground_duty(ground_duty_path)

    print("Searching for an example flight...")

    for index, row in unassigned_df.iterrows():
        flight_id = row['flight_id']
        flight_info = flights_df[flights_df['id'] == flight_id].iloc[0]
        flight_start = datetime.strptime(flight_info['std'], '%Y/%m/%d %H:%M')
        flight_end = datetime.strptime(flight_info['sta'], '%Y/%m/%d %H:%M')
        dep_airport = flight_info['depaAirport']

        all_qualified_crews = get_qualified_crews_for_flight(flight_id, match_path)
        qualified_crews = [c for c in all_qualified_crews if c not in crews_with_ground_duty]
        
        if not qualified_crews: continue

        available_crews_at_location = []

        for crew_id in qualified_crews:
            if crew_id not in crew_schedules:
                # Cannot confirm location for crews with no schedule.
                continue

            is_busy = any(flight_start < task['end'] and flight_end > task['start'] for task in crew_schedules[crew_id]['tasks'])
            
            if not is_busy:
                crew_location = crew_schedules[crew_id].get('base')
                for task in sorted(crew_schedules[crew_id]['tasks'], key=lambda x: x['end']):
                    if task['end'] < flight_start:
                        crew_location = task['arrival_location']
                    else: break
                
                if crew_location == dep_airport:
                    available_crews_at_location.append(crew_id)
        
        if available_crews_at_location:
            print("\n" + "="*60)
            print("Found an Example!")
            print("="*60)
            print(f"\nUnassigned Flight: {flight_id}")
            print(f"  - Departure: {dep_airport} at {flight_start}")
            print(f"  - Arrival:   {flight_info['arriAirport']} at {flight_end}")
            
            print(f"\nThis flight was unassigned, but the following crew member(s) were qualified, available, and at the correct departure airport:")
            
            for crew_id in available_crews_at_location[:2]: # Display first 2 examples
                print("\n" + "-"*60)
                print(f"Schedule for Crew: {crew_id}")
                print(f"Base: {crew_schedules[crew_id]['base']}")
                print(f"Last Known Location before flight: {dep_airport}")
                print("-"*60)
                print(crew_schedules[crew_id]['raw_schedule'])
            
            print("\n" + "="*60)
            print("This suggests the solver chose not to assign this flight due to a complex rule (duty/rest time) or a heuristic decision.")
            print("="*60)
            return # Stop after finding the first example

    print("Could not find a clear example in the dataset.")


if __name__ == "__main__":
    find_and_display_example(
        unassigned_path='unassigned_flights.txt',
        flight_path='data/0703/flight.csv',
        match_path='data/0703/crewLegMatch.csv',
        report_path='heuristic/report/0703/schedule_report.txt',
        ground_duty_path='data/0703/groundDuty.csv'
    ) 