import pandas as pd
from collections import Counter
import matplotlib.pyplot as plt
import networkx as nx
import sys

def analyze_connections(data_path):
    """
    Analyzes flight data to understand airport connections.

    Args:
        data_path (str): Path to the flight.csv file.
    """
    print(f"Analyzing flight data from {data_path}...")
    try:
        flights_df = pd.read_csv(data_path)
    except FileNotFoundError:
        print(f"Error: The file {data_path} was not found.")
        print("Please make sure the flight data is available at that path.")
        return

    origin_col = 'depaAirport'
    dest_col = 'arriAirport'

    if origin_col not in flights_df.columns or dest_col not in flights_df.columns:
        print(f"Error: Expected columns '{origin_col}' and '{dest_col}' not in {data_path}")
        print(f"Available columns: {flights_df.columns.tolist()}")
        return

    # 1. Airport traffic (number of flights)
    all_airports = list(flights_df[origin_col]) + list(flights_df[dest_col])
    airport_counts = Counter(all_airports)

    print("\n--- Airport Traffic Analysis ---")
    print("Top 20 busiest airports:")
    for airport, count in airport_counts.most_common(20):
        print(f"- {airport}: {count} flights")

    # 2. Busiest routes
    routes = list(zip(flights_df[origin_col], flights_df[dest_col]))
    route_counts = Counter(routes)

    print("\n--- Route Analysis ---")
    print("Top 20 busiest routes:")
    for (origin, dest), count in route_counts.most_common(20):
        print(f"- {origin} -> {dest}: {count} flights")

    # 3. Source/Sink airports
    origin_airports = set(flights_df[origin_col])
    dest_airports = set(flights_df[dest_col])

    source_only = origin_airports - dest_airports
    sink_only = dest_airports - origin_airports

    print("\n--- Connectivity Analysis ---")
    if source_only:
        print(f"Airports that are only departure points: {source_only}")
    else:
        print("No airports are only departure points.")

    if sink_only:
        print(f"Airports that are only arrival points: {sink_only}")
    else:
        print("No airports are only arrival points.")
        
    # 4. Create a graph for visualization
    print("\nGenerating airport network graph...")
    G = nx.from_pandas_edgelist(flights_df, source=origin_col, target=dest_col, create_using=nx.DiGraph())
    
    plt.figure(figsize=(20, 20))
    pos = nx.spring_layout(G, k=0.5, iterations=50)
    nx.draw(G, pos, with_labels=True, node_size=100, alpha=0.8, arrows=True, font_size=10)
    plt.title('Airport Connection Network')
    
    output_filename = data_path.replace('.csv', '_network.png').split('/')[-1]
    plt.savefig(output_filename)
    print(f"Saved airport network graph to {output_filename}")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        data_file = sys.argv[1]
    else:
        data_file = 'data/0711/flight.csv'
        print(f"No data file provided, using default: {data_file}")
    analyze_connections(data_file) 