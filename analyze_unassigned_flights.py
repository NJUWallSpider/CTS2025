import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import sys

def analyze_unassigned(unassigned_path='unassigned_flights.txt', flights_path='data/0711/flight.csv'):
    """
    Analyzes the properties of unassigned flights.

    Args:
        unassigned_path (str): Path to the text file with unassigned flight IDs.
        flights_path (str): Path to the flight.csv file.
    """
    print(f"Analyzing unassigned flights from {unassigned_path}...")
    try:
        unassigned_ids = pd.read_csv(unassigned_path, header=None)[0].tolist()
    except FileNotFoundError:
        print(f"Error: The file {unassigned_path} was not found.")
        return
        
    try:
        flights_df = pd.read_csv(flights_path)
    except FileNotFoundError:
        print(f"Error: The file {flights_path} was not found.")
        return

    unassigned_df = flights_df[flights_df['id'].isin(unassigned_ids)].copy()

    if unassigned_df.empty:
        print("No unassigned flights found in the flight data.")
        return

    print(f"Found {len(unassigned_df)} unassigned flights to analyze.")

    # --- Analysis ---

    # 1. Airport Analysis
    print("\n--- Airport Analysis of Unassigned Flights ---")
    unassigned_depa_counts = unassigned_df['depaAirport'].value_counts()
    unassigned_arri_counts = unassigned_df['arriAirport'].value_counts()

    print("Top 10 departure airports for unassigned flights:")
    print(unassigned_depa_counts.head(10))

    print("\nTop 10 arrival airports for unassigned flights:")
    print(unassigned_arri_counts.head(10))
    
    # Check for flights to/from the source/sink airports
    wigy_flights = unassigned_df[unassigned_df['depaAirport'] == 'WIGY']
    tfkn_flights = unassigned_df[unassigned_df['arriAirport'] == 'TFKN']

    print(f"\nNumber of unassigned flights from WIGY (source-only): {len(wigy_flights)}")
    print(f"Number of unassigned flights to TFKN (sink-only): {len(tfkn_flights)}")


    # 2. Fleet Analysis
    print("\n--- Fleet Analysis of Unassigned Flights ---")
    fleet_counts = unassigned_df['fleet'].value_counts()
    print("Fleet distribution for unassigned flights:")
    print(fleet_counts)

    # 3. Time of Day Analysis
    unassigned_df['std_hour'] = pd.to_datetime(unassigned_df['std']).dt.hour
    plt.figure(figsize=(12, 6))
    sns.histplot(unassigned_df['std_hour'], bins=24, kde=False)
    plt.title('Distribution of Unassigned Flights by Departure Hour')
    plt.xlabel('Hour of Day')
    plt.ylabel('Number of Unassigned Flights')
    plt.xticks(range(24))
    plt.grid(axis='y', alpha=0.75)
    
    output_filename = 'unassigned_flights_by_hour.png'
    plt.savefig(output_filename)
    print(f"\nSaved unassigned flights by hour graph to {output_filename}")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        flights_file = sys.argv[1]
    else:
        flights_file = 'data/0703/flight.csv'
        print(f"No flights file provided, using default: {flights_file}")
    
    if len(sys.argv) > 2:
        unassigned_file = sys.argv[2]
    else:
        unassigned_file = 'unassigned_flights.txt'
        print(f"No unassigned flights file provided, using default: {unassigned_file}")

    analyze_unassigned(unassigned_path=unassigned_file, flights_path=flights_file) 