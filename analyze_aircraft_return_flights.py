import pandas as pd
import os
from collections import defaultdict

def analyze_aircraft_return_flights(data_folder):
    flights_path = os.path.join(data_folder, 'flight.csv')

    if not os.path.exists(flights_path):
        print(f"Skipping {data_folder}, flights.csv not found.")
        return

    flights = pd.read_csv(flights_path)
    
    # Group flights by aircraft
    flights_by_aircraft = flights.groupby('aircraftNo')

    unbalanced_aircraft_count = 0
    total_aircraft = len(flights_by_aircraft)
    
    print(f"--- Analysis for {os.path.basename(data_folder)} ---")

    for aircraft_no, aircraft_flights in flights_by_aircraft:
        routes = set()
        for _, flight in aircraft_flights.iterrows():
            routes.add((flight['depaAirport'], flight['arriAirport']))

        is_balanced = True
        
        # Check for return flights
        for dep, arr in routes:
            if (arr, dep) not in routes:
                is_balanced = False
                break

        if not is_balanced:
            unbalanced_aircraft_count += 1

    balanced_aircraft_count = total_aircraft - unbalanced_aircraft_count

    print(f"Total aircraft: {total_aircraft}")
    print(f"Aircraft with balanced return flights: {balanced_aircraft_count}")
    print(f"Aircraft with unbalanced return flights: {unbalanced_aircraft_count}")
        
    print("-" * (20 + len(os.path.basename(data_folder))))


if __name__ == "__main__":
    data_root = 'data'
    datasets = ['0606', '0623', '0703']

    for dataset in datasets:
        data_folder_path = os.path.join(data_root, dataset)
        analyze_aircraft_return_flights(data_folder_path) 