import pandas as pd

def analyze_station_concentration():
    """
    Analyzes and compares the concentration of crew bases and stay stations.
    """
    try:
        crew_df = pd.read_csv('data/0623/crew.csv')
    except FileNotFoundError as e:
        print(f"Error loading data file: {e}")
        return

    # Analyze base concentration
    base_counts = crew_df['base'].value_counts()
    num_unique_bases = len(base_counts)
    
    print("Base Station Analysis")
    print("="*30)
    print(f"Total number of unique bases: {num_unique_bases}")
    print("\nTop 5 base stations by crew count:")
    print(base_counts.head().to_string())
    print(f"\nTop 5 bases account for {base_counts.head().sum() / len(crew_df) * 100:.2f}% of all crews.")

    # Analyze stay station concentration
    # The stayStation column might have multiple stations, so we need to process it.
    # Assuming they are comma-separated if multiple, but let's check first.
    # For now, let's assume it's a single station per crew as seen in the head of the file.
    stay_station_counts = crew_df['stayStation'].value_counts()
    num_unique_stay_stations = len(stay_station_counts)

    print("\n\nStay Station Analysis")
    print("="*30)
    print(f"Total number of unique stay stations: {num_unique_stay_stations}")
    print("\nTop 5 stay stations by crew count:")
    print(stay_station_counts.head().to_string())
    print(f"\nTop 5 stay stations account for {stay_station_counts.head().sum() / len(crew_df) * 100:.2f}% of all crews.")
    
    # Conclusion
    print("\n\nConclusion")
    print("="*30)
    if num_unique_stay_stations < num_unique_bases:
        print("Stay stations appear to be more concentrated than bases.")
    elif num_unique_stay_stations > num_unique_bases:
        print("Bases appear to be more concentrated than stay stations.")
    else:
        print("Bases and stay stations have the same number of unique locations.")
        
    print(f"There are {num_unique_bases} bases and {num_unique_stay_stations} stay stations.")


if __name__ == '__main__':
    analyze_station_concentration() 