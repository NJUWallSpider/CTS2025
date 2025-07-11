import pandas as pd
import os

def analyze_dataset(data_folder):
    layover_stations_path = os.path.join(data_folder, 'layoverStation.csv')
    flights_path = os.path.join(data_folder, 'flight.csv')

    if not os.path.exists(layover_stations_path) or not os.path.exists(flights_path):
        print(f"Skipping {data_folder}, required files not found.")
        return

    layover_stations = pd.read_csv(layover_stations_path)
    base_airports = set(layover_stations['airport'])

    flights = pd.read_csv(flights_path)

    unconnected_flights = 0
    for index, flight in flights.iterrows():
        dep_apt = flight['depaAirport']
        arr_apt = flight['arriAirport']
        if dep_apt not in base_airports and arr_apt not in base_airports:
            unconnected_flights += 1
            # print(f"Unconnected flight: {flight['flightId']} from {dep_apt} to {arr_apt}")

    print(f"--- Analysis for {os.path.basename(data_folder)} ---")
    if unconnected_flights == 0:
        print("All flights are connected to at least one base airport.")
    else:
        print(f"Found {unconnected_flights} flights not connected to any base airport.")
    print(f"Total flights: {len(flights)}")
    print(f"Number of base airports: {len(base_airports)}")
    print("-" * (20 + len(os.path.basename(data_folder))))


if __name__ == "__main__":
    data_root = 'data'
    datasets = ['0606', '0623', '0703']

    for dataset in datasets:
        data_folder_path = os.path.join(data_root, dataset)
        analyze_dataset(data_folder_path) 