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
            crew_id = "Crew_" + chunk.split('\n')[0].strip().split('_')[1]
            crew_schedules[crew_id] = []
            
            time_matches = re.findall(r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}) - (\d{4}-\d{2}-\d{2} \d{2}:\d{2})\]', chunk)
            
            for start_str, end_str in time_matches:
                crew_schedules[crew_id].append({
                    "start": datetime.strptime(start_str, '%Y-%m-%d %H:%M'),
                    "end": datetime.strptime(end_str, '%Y-%m-%d %H:%M')
                })
        except (IndexError, ValueError) as e:
            # Handle cases where parsing might fail for a chunk
            print(f"Could not parse chunk for crew: {chunk.splitlines()[0]}")
            continue
            
    return crew_schedules

def get_qualified_crews_for_flight(flight_id, match_file_path):
    qualified_crews = []
    chunk_size = 100000  # Process in chunks to handle large file
    for chunk in pd.read_csv(match_file_path, chunksize=chunk_size):
        # The file might have quotes around the IDs, remove them.
        chunk['crewId'] = chunk['crewId'].str.strip('\'"')
        chunk['legId'] = chunk['legId'].str.strip('\'"')
        
        qualified = chunk[chunk['legId'] == flight_id]
        if not qualified.empty:
            qualified_crews.extend(qualified['crewId'].tolist())
    return qualified_crews

def analyze_unassigned_flights(unassigned_path, flight_path, match_path, report_path, num_samples=5):
    unassigned_df = pd.read_csv(unassigned_path, header=None, names=['flight_id'])
    flights_df = pd.read_csv(flight_path)
    crew_schedules = parse_schedule_report(report_path)
    sample_flights = unassigned_df.head(num_samples)

    print("======================================================")
    print("      Analysis of Unassigned Flights")
    print("======================================================")

    for _, row in sample_flights.iterrows():
        flight_id = row['flight_id']
        flight_info = flights_df[flights_df['id'] == flight_id].iloc[0]
        
        flight_start = datetime.strptime(flight_info['std'], '%Y/%m/%d %H:%M')
        flight_end = datetime.strptime(flight_info['sta'], '%Y/%m/%d %H:%M')

        print(f"\n--- Analyzing Flight: {flight_id} ---")
        print(f"  - Fleet: {flight_info['fleet']}, Dep: {flight_info['depaAirport']} @ {flight_start}, Arr: {flight_info['arriAirport']} @ {flight_end}")

        qualified_crews = get_qualified_crews_for_flight(flight_id, match_path)
        
        if not qualified_crews:
            print("  - Reason: No crews are qualified for this specific flight leg in crewLegMatch.csv.")
            continue

        print(f"\n  - Found {len(qualified_crews)} qualified crews. Checking availability...")
        
        available_crews_count = 0
        for crew_id in qualified_crews:
            is_available = True
            if crew_id in crew_schedules:
                for task in crew_schedules[crew_id]:
                    if max(task['start'], flight_start) < min(task['end'], flight_end):
                        is_available = False
                        break
            if is_available:
                available_crews_count += 1
        
        if available_crews_count > 0:
            print(f"\n  - Result: {available_crews_count} qualified crews were potentially available (not busy).")
            print("  - Possible Reasons for Not Assigning:")
            print("    - Location Mismatch: Crew was not at the departure airport.")
            print("    - Heuristic Choice: Solver found a 'better' schedule, leaving this flight out.")
            print("    - Other Rules: Violation of complex rules like duty time or rest periods.")
        else:
            print("\n  - Result: All qualified crews for this flight were already scheduled for other tasks at the same time.")

    print("\n======================================================")

if __name__ == "__main__":
    analyze_unassigned_flights(
        unassigned_path='unassigned_flights.txt',
        flight_path='data/0703/flight.csv',
        match_path='data/0703/crewLegMatch.csv',
        report_path='heuristic/report/schedule_report.txt'
    ) 