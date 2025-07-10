import re
import os

def get_layover_stations(filepath):
    """
    Reads a list of layover stations from a given CSV file.
    """
    if not os.path.exists(filepath):
        print(f"Error: Layover station file not found at {filepath}")
        return set()
        
    stations = set()
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            header = f.readline().strip() # Read header
            for line in f:
                station = line.strip().split(',')[0]
                if station:
                    stations.add(station)
    except Exception as e:
        print(f"Error reading layover stations: {e}")
    return stations

def analyze_schedule_report_details(report_path, layover_stations):
    """
    Analyzes a schedule report to find:
    1. How many FDPs end with a flight vs. a bus.
    2. How many FDPs end with a positioning task from a non-layover to a layover station.
    3. How many FDPs start with a task from a layover station.
    """
    if not os.path.exists(report_path):
        print(f"Error: Report file not found at {report_path}")
        return

    fdp_ends_with_flight = 0
    fdp_ends_with_bus = 0
    positioning_tasks_count = 0
    fdp_starts_from_layover = 0
    
    in_duty_period = False
    is_fdp = False
    current_dp_tasks = []

    try:
        with open(report_path, 'r', encoding='utf-8') as f:
            for line in f:
                # Detect start of a new duty period
                if "=== Duty Period" in line:
                    # Process the previous duty period before starting a new one
                    if in_duty_period and is_fdp and current_dp_tasks:
                        last_task = current_dp_tasks[-1]
                        if last_task['type'] == 'flight':
                            fdp_ends_with_flight += 1
                        elif last_task['type'] == 'bus':
                            fdp_ends_with_bus += 1
                        
                        if last_task['type'] == 'bus' and last_task['depa'] not in layover_stations and last_task['arri'] in layover_stations:
                            positioning_tasks_count += 1
                        
                        first_task = current_dp_tasks[0]
                        if first_task['depa'] in layover_stations:
                            fdp_starts_from_layover += 1
                    
                    # Reset for the new duty period
                    in_duty_period = True
                    is_fdp = False
                    current_dp_tasks = []

                # Detect end of a duty period block
                elif ("--- Rest Period ---" in line or "--------------------------------------------------" in line) and in_duty_period:
                    if is_fdp and current_dp_tasks:
                        last_task = current_dp_tasks[-1]
                        if last_task['type'] == 'flight':
                            fdp_ends_with_flight += 1
                        elif last_task['type'] == 'bus':
                            fdp_ends_with_bus += 1
                        
                        if last_task['type'] == 'bus' and last_task['depa'] not in layover_stations and last_task['arri'] in layover_stations:
                            positioning_tasks_count += 1

                        first_task = current_dp_tasks[0]
                        if first_task['depa'] in layover_stations:
                            fdp_starts_from_layover += 1
                    
                    in_duty_period = False
                    is_fdp = False
                    current_dp_tasks = []

                # If inside a duty period, parse tasks
                if in_duty_period:
                    flight_match = re.search(r'Flight Flt_\d+\s+\(Aft_\d+\)\s+\(([^)]+?)\s+->\s+([^)]+?)\)', line)
                    bus_match = re.search(r'Bus ddh_\d+\s+\(([^)]+?)\s+->\s+([^)]+?)\)', line)
                    
                    if flight_match:
                        is_fdp = True
                        depa, arri = flight_match.groups()
                        current_dp_tasks.append({'type': 'flight', 'depa': depa.strip(), 'arri': arri.strip()})
                    elif bus_match:
                        depa, arri = bus_match.groups()
                        current_dp_tasks.append({'type': 'bus', 'depa': depa.strip(), 'arri': arri.strip()})

        # Process the very last duty period in the file if it exists
        if in_duty_period and is_fdp and current_dp_tasks:
            last_task = current_dp_tasks[-1]
            if last_task['type'] == 'flight':
                fdp_ends_with_flight += 1
            elif last_task['type'] == 'bus':
                fdp_ends_with_bus += 1

            if last_task['type'] == 'bus' and last_task['depa'] not in layover_stations and last_task['arri'] in layover_stations:
                positioning_tasks_count += 1
            
            first_task = current_dp_tasks[0]
            if first_task['depa'] in layover_stations:
                fdp_starts_from_layover += 1

        print(f"Analysis of report: {report_path}")
        print("-" * 30)
        print(f"Flight Duty Periods ending with a flight: {fdp_ends_with_flight}")
        print(f"Flight Duty Periods ending with a bus: {fdp_ends_with_bus}")
        print(f"FDPs ending with a positioning bus to a layover station: {positioning_tasks_count}")
        print(f"FDPs starting from a layover station: {fdp_starts_from_layover}")
        print("-" * 30)

    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    report_to_analyze = 'heuristic/report/0606/schedule_report.txt'
    layover_file = 'data/0606/layoverStation.csv'
    
    stations = get_layover_stations(layover_file)
    if stations:
        analyze_schedule_report_details(report_to_analyze, stations) 