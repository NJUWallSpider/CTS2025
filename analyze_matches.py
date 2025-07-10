import pandas as pd
import os
import numpy as np

def analyze_dataset(data_path, flight_path):
    print(f"--- Analyzing Dataset: {data_path} ---")

    # Load crew leg match data
    try:
        match_df = pd.read_csv(data_path, dtype={'crewId': str, 'legId': str})
        # The 0606 dataset has extra quotes around the IDs that need to be stripped
        if '0606' in data_path:
            match_df['crewId'] = match_df['crewId'].str.strip('"')
            match_df['legId'] = match_df['legId'].str.strip('"')

    except FileNotFoundError:
        print(f"Error: Could not find {data_path}")
        return

    # Load flight data to get the full list of flights
    try:
        flight_df = pd.read_csv(flight_path, dtype={'id': str})
    except FileNotFoundError:
        print(f"Error: Could not find {flight_path}")
        return

    # Get total counts
    total_matches = len(match_df)
    total_flights_in_system = len(flight_df['id'].unique())
    total_crews_in_matches = len(match_df['crewId'].unique())
    total_flights_in_matches = len(match_df['legId'].unique())

    print(f"Total crew-leg matches: {total_matches}")
    print(f"Total unique crews in match file: {total_crews_in_matches}")
    print(f"Total unique flights in match file: {total_flights_in_matches}")
    print(f"Total unique flights in system: {total_flights_in_system}")

    # Flights with no qualified crews
    flights_with_no_crew = set(flight_df['id']) - set(match_df['legId'])
    print(f"Number of flights with NO qualified crew: {len(flights_with_no_crew)}")

    # Analysis of flights per crew
    if total_matches > 0:
        flights_per_crew = match_df.groupby('crewId')['legId'].nunique()
        print("\nFlights per Crew stats:")
        print(f"  - Average: {flights_per_crew.mean():.2f}")
        print(f"  - Median:  {flights_per_crew.median():.2f}")
        print(f"  - Std Dev: {flights_per_crew.std():.2f}")
        print(f"  - Min:     {flights_per_crew.min()}")
        print(f"  - Max:     {flights_per_crew.max()}")

    # Analysis of crews per flight
    if total_matches > 0:
        crews_per_flight = match_df.groupby('legId')['crewId'].nunique()
        print("\nCrews per Flight stats:")
        print(f"  - Average: {crews_per_flight.mean():.2f}")
        print(f"  - Median:  {crews_per_flight.median():.2f}")
        print(f"  - Std Dev: {crews_per_flight.std():.2f}")
        print(f"  - Min:     {crews_per_flight.min()}")
        print(f"  - Max:     {crews_per_flight.max()}")
    
    print("-" * (len(data_path) + 24))


if __name__ == "__main__":
    datasets = ["0606", "0623", "0703"]
    for ds in datasets:
        match_file = os.path.join("data", ds, "crewLegMatch.csv")
        flight_file = os.path.join("data", ds, "flight.csv")
        analyze_dataset(match_file, flight_file)
        print("\n") 