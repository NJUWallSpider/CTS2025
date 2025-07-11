import pandas as pd

def analyze_crew_initial_airports():
    """
    Analyzes the relationship between the initial airport of the crews and their base.
    """
    # Load the datasets
    try:
        crew_df = pd.read_csv('data/0623/crew.csv')
        flight_df = pd.read_csv('data/0623/flight.csv')
        roster_df = pd.read_csv('heuristic/report/0623/rosterResult.csv')
    except FileNotFoundError as e:
        print(f"Error loading data file: {e}")
        return

    # Create a dictionary mapping crewId to base
    crew_base_map = pd.Series(crew_df.base.values, index=crew_df.crewId).to_dict()

    # Create a dictionary mapping flightId to departure airport
    flight_depa_map = pd.Series(flight_df.depaAirport.values, index=flight_df.id).to_dict()

    # Get the first task for each crew
    # The roster is ordered by sequence of tasks for each crew
    first_task_df = roster_df.drop_duplicates(subset='crewId', keep='first')

    # Get the initial airport for each crew
    def get_initial_airport(row):
        task_id = row['taskId']
        if task_id.startswith('Flt'):
            return flight_depa_map.get(task_id)
        # Handle other task types like ground duty or bus if necessary
        return None

    first_task_df.loc[:, 'initial_airport'] = first_task_df.apply(get_initial_airport, axis=1)

    # Get the base for each crew
    first_task_df.loc[:, 'base'] = first_task_df['crewId'].map(crew_base_map)

    # Analyze the relationship
    on_base_starts = 0
    off_base_starts = 0
    off_base_details = []

    for _, row in first_task_df.iterrows():
        if row['initial_airport'] and row['base']:
            if row['initial_airport'] == row['base']:
                on_base_starts += 1
            else:
                off_base_starts += 1
                off_base_details.append({
                    'crewId': row['crewId'],
                    'base': row['base'],
                    'initial_airport': row['initial_airport']
                })

    # Print the report
    print("Crew Initial Airport Analysis")
    print("="*30)
    print(f"Total crews with flights: {len(first_task_df)}")
    print(f"Crews starting at their base: {on_base_starts}")
    print(f"Crews starting at a different airport: {off_base_starts}")
    print("-" * 30)

    if off_base_starts > 0:
        print("\nDetails of crews starting off-base:")
        off_base_df = pd.DataFrame(off_base_details)
        print(off_base_df.to_string())

if __name__ == '__main__':
    analyze_crew_initial_airports() 