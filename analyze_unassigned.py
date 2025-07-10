import pandas as pd
import re
from datetime import datetime

def parse_schedule_report(report_path):
    with open(report_path, 'r') as f:
        content = f.read()
    
    crew_schedules = {}
    crew_chunks = content.split('Crew ID: ')
    
    for chunk in crew_chunks[1:]:
        try:
            crew_id_full = chunk.split('\n')[0].strip()
            crew_id = "Crew_" + crew_id_full.split('_')[1]
            
            # Store the raw schedule text for later display
            crew_schedules[crew_id] = {"raw_schedule": "Crew ID: " + chunk.strip(), "tasks": []}
            
            time_matches = re.findall(r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}) - (\d{4}-\d{2}-\d{2} \d{2}:\d{2})\]', chunk)
            
            for start_str, end_str in time_matches:
                crew_schedules[crew_id]["tasks"].append({
                    "start": datetime.strptime(start_str, '%Y-%m-%d %H:%M'),
                    "end": datetime.strptime(end_str, '%Y-%m-%d %H:%M')
                })
        except (IndexError, ValueError) as e:
            print(f"Could not parse chunk for crew: {chunk.splitlines()[0]}")
            continue
            
    return crew_schedules

def get_qualified_crews_for_flight(flight_id, match_file_path):
    qualified_crews = []
    chunk_size = 100000
    for chunk in pd.read_csv(match_file_path, chunksize=chunk_size):
        chunk['crewId'] = chunk['crewId'].str.strip('\'"')
        chunk['legId'] = chunk['legId'].str.strip('\'"')
        
        qualified = chunk[chunk['legId'] == flight_id]
        if not qualified.empty:
            qualified_crews.extend(qualified['crewId'].tolist())
    return qualified_crews

def analyze_and_display_schedules(unassigned_path, flight_path, match_path, report_path, num_crews_to_display=3):
    unassigned_df = pd.read_csv(unassigned_path, header=None, names=['flight_id'])
    flights_df = pd.read_csv(flight_path)
    crew_schedules = parse_schedule_report(report_path)
    
    # --- Focus on the first unassigned flight ---
    flight_id = unassigned_df['flight_id'].iloc[0]
    flight_info = flights_df[flights_df['id'] == flight_id].iloc[0]
    flight_start = datetime.strptime(flight_info['std'], '%Y/%m/%d %H:%M')
    flight_end = datetime.strptime(flight_info['sta'], '%Y/%m/%d %H:%M')

    print("======================================================")
    print(f"Analyzing Schedules for Unassigned Flight: {flight_id}")
    print("======================================================")
    print(f"Flight Details: Fleet {flight_info['fleet']}, Dep: {flight_info['depaAirport']} @ {flight_start}, Arr: {flight_info['arriAirport']} @ {flight_end}")

    qualified_crews = get_qualified_crews_for_flight(flight_id, match_path)
    
    if not qualified_crews:
        print("\nNo crews are qualified for this specific flight leg.")
        return

    print(f"\nFound {len(qualified_crews)} qualified crews. Finding available ones...")
    
    available_crew_ids = []
    for crew_id in qualified_crews:
        is_available = True
        if crew_id in crew_schedules and crew_schedules[crew_id]["tasks"]:
            for task in crew_schedules[crew_id]["tasks"]:
                if max(task['start'], flight_start) < min(task['end'], flight_end):
                    is_available = False
                    break
        if is_available:
            available_crew_ids.append(crew_id)
    
    if not available_crew_ids:
        print("\nAll qualified crews were busy with other assignments.")
        return

    print(f"\nFound {len(available_crew_ids)} available qualified crews. Displaying schedules for the first {num_crews_to_display}:")
    print("------------------------------------------------------")

    for i, crew_id in enumerate(available_crew_ids[:num_crews_to_display]):
        print(f"\n--- Schedule for Available Crew: {crew_id} ---\n")
        if crew_id in crew_schedules:
            print(crew_schedules[crew_id]["raw_schedule"])
        else:
            # This case can happen if a crew is qualified but has no scheduled tasks at all.
            print(f"Crew {crew_id} is qualified but has no assigned tasks in the schedule report.")
        print("\n------------------------------------------------------")


if __name__ == "__main__":
    analyze_and_display_schedules(
        unassigned_path='unassigned_flights.txt',
        flight_path='data/0703/flight.csv',
        match_path='data/0703/crewLegMatch.csv',
        report_path='heuristic/report/schedule_report.txt'
    ) 