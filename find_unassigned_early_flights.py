import pandas as pd
from datetime import datetime

def find_unassigned_early_flights():
    """
    Finds and analyzes unassigned flights from May 1st and May 2nd.
    """
    try:
        flight_df = pd.read_csv('data/0623/flight.csv')
        roster_df = pd.read_csv('heuristic/report/0623/rosterResult.csv')
        crew_df = pd.read_csv('data/0623/crew.csv')
    except FileNotFoundError as e:
        print(f"Error loading data file: {e}")
        print("Please ensure the data files are in the 'data/0623/' directory.")
        return

    # Convert std to datetime objects for filtering
    flight_df['std_datetime'] = pd.to_datetime(flight_df['std'])

    # Filter for flights on May 1st and May 2nd
    start_date = datetime(2025, 5, 1)
    end_date = datetime(2025, 5, 2, 23, 59, 59)
    early_flights_df = flight_df[(flight_df['std_datetime'] >= start_date) & (flight_df['std_datetime'] <= end_date)]

    # Get the set of assigned flight IDs
    assigned_flight_ids = set(roster_df[roster_df['taskId'].str.startswith('Flt')]['taskId'])

    # Find unassigned flights
    unassigned_flights_df = early_flights_df[~early_flights_df['id'].isin(assigned_flight_ids)].copy()
    
    num_unassigned = len(unassigned_flights_df)
    print(f"Found {num_unassigned} unassigned flights on May 1st and 2nd.")

    if num_unassigned == 0:
        print("All flights on these dates were assigned.")
        return

    # --- Analysis ---
    print("\n--- Analysis of Unassigned Flights ---")

    # 1. Analyze by departure time
    unassigned_flights_df.loc[:, 'hour'] = unassigned_flights_df['std_datetime'].dt.hour
    hourly_counts = unassigned_flights_df['hour'].value_counts().sort_index()
    
    print("\n1. Unassigned flights by hour of the day:")
    print(hourly_counts.to_string())
    
    early_morning_flights = unassigned_flights_df[unassigned_flights_df['hour'] < 8]
    print(f"\n   - {len(early_morning_flights)} ({len(early_morning_flights)/num_unassigned:.1%}) of unassigned flights depart before 8 AM.")

    # 2. Analyze by departure airport
    airport_counts = unassigned_flights_df['depaAirport'].value_counts()
    print("\n2. Top 10 departure airports for unassigned flights:")
    print(airport_counts.head(10).to_string())

    # 3. Cross-reference with crew bases
    crew_bases = set(crew_df['base'])
    unassigned_flights_df.loc[:, 'depa_is_base'] = unassigned_flights_df['depaAirport'].isin(crew_bases)
    
    base_departure_count = unassigned_flights_df['depa_is_base'].sum()
    outstation_departure_count = len(unassigned_flights_df) - base_departure_count
    
    print("\n3. Departure airport type:")
    print(f"   - Flights departing from a crew base: {base_departure_count}")
    print(f"   - Flights departing from an outstation: {outstation_departure_count} ({outstation_departure_count/num_unassigned:.1%})")

    print("\n--- Detailed List of Unassigned Flights (sorted by time) ---")
    print(unassigned_flights_df[['id', 'std_datetime', 'depaAirport', 'arriAirport', 'depa_is_base']].sort_values('std_datetime').to_string())


if __name__ == '__main__':
    find_unassigned_early_flights() 