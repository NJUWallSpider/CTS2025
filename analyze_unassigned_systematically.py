import pandas as pd
import re
from datetime import datetime
from collections import defaultdict

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

def analyze_all_unassigned(unassigned_path, flight_path, match_path, report_path, ground_duty_path, num_samples=20):
    unassigned_df = pd.read_csv(unassigned_path, header=None, names=['flight_id']).head(num_samples)
    flights_df = pd.read_csv(flight_path)
    crew_schedules = get_crew_schedules_and_locations(report_path)
    crews_with_ground_duty = get_crews_with_ground_duty(ground_duty_path)
    categories = defaultdict(list)
    print(f"Analyzing {len(unassigned_df)} unassigned flights (from a pool of crews with no ground duty)...")
    for index, row in unassigned_df.iterrows():
        flight_id = row['flight_id']
        flight_info = flights_df[flights_df['id'] == flight_id].iloc[0]
        flight_start = datetime.strptime(flight_info['std'], '%Y/%m/%d %H:%M')
        flight_end = datetime.strptime(flight_info['sta'], '%Y/%m/%d %H:%M')
        dep_airport = flight_info['depaAirport']
        all_qualified_crews = get_qualified_crews_for_flight(flight_id, match_path)
        qualified_crews = [c for c in all_qualified_crews if c not in crews_with_ground_duty]
        if not qualified_crews:
            categories['no_qualified_crews_without_ground_duty'].append(flight_id)
            continue
        is_someone_available_and_at_location = False
        is_someone_available_somewhere = False
        for crew_id in qualified_crews:
            if crew_id not in crew_schedules:
                is_someone_available_somewhere = True
                # Cannot determine location if crew has no schedule. Assume base and check.
                # This requires loading crew base data, which is an enhancement.
                # For now, we can't confirm they are at the right location.
                continue
            is_busy = any(flight_start < task['end'] and flight_end > task['start'] for task in crew_schedules[crew_id]['tasks'])
            if not is_busy:
                is_someone_available_somewhere = True
                crew_location = crew_schedules[crew_id].get('base')
                for task in sorted(crew_schedules[crew_id]['tasks'], key=lambda x: x['end']):
                    if task['end'] < flight_start:
                        crew_location = task['arrival_location']
                    else: break
                if crew_location == dep_airport:
                    is_someone_available_and_at_location = True
                    break
        if is_someone_available_and_at_location:
            categories['available_at_location'].append(flight_id)
        elif is_someone_available_somewhere:
            categories['available_wrong_location'].append(flight_id)
        else:
            categories['all_qualified_busy'].append(flight_id)
    print("\n" + "="*54)
    print("      Analysis of Unassigned Flights (No Ground Duty Crews)")
    print("="*54)
    print(f"Total Unassigned Flights Analyzed: {len(unassigned_df)}")
    print(f" - No Qualified Crews (w/o ground duty): {len(categories['no_qualified_crews_without_ground_duty'])}")
    print(f" - All Qualified Crews Busy: {len(categories['all_qualified_busy'])}")
    print(f" - Available but Wrong Location: {len(categories['available_wrong_location'])}")
    print(f" - Available and at Correct Location: {len(categories['available_at_location'])}")
    print("="*54)

if __name__ == "__main__":
    analyze_all_unassigned(
        unassigned_path='unassigned_flights.txt',
        flight_path='data/0703/flight.csv',
        match_path='data/0703/crewLegMatch.csv',
        report_path='heuristic/report/0703/schedule_report.txt',
        ground_duty_path='data/0703/groundDuty.csv'
    ) 