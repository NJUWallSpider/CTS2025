
import pandas as pd
from itertools import combinations
import os

def analyze_flight_qualifications(data_path='data/0703'):
    """
    Analyzes if crews from the same base have similar flight qualifications.

    Args:
        data_path (str): The path to the dataset directory.
    """
    crew_file = os.path.join(data_path, 'crew.csv')
    crew_leg_match_file = os.path.join(data_path, 'crewLegMatch.csv')

    if not all(os.path.exists(f) for f in [crew_file, crew_leg_match_file]):
        print(f"Data files not found in {data_path}")
        return

    # Load data
    crew_df = pd.read_csv(crew_file)
    crew_leg_match_df = pd.read_csv(crew_leg_match_file)

    # Get flight qualifications for each crew
    crew_flights = crew_leg_match_df.groupby('crewId')['legId'].apply(set).to_dict()

    # Get base for each crew
    crew_base = crew_df.set_index('crewId')['base'].to_dict()

    # Group crews by base
    base_crews = {}
    for crew_id, base in crew_base.items():
        if base not in base_crews:
            base_crews[base] = []
        base_crews[base].append(crew_id)

    print(f"Analyzing flight qualification similarity for crews by base in {data_path}...\n")

    # Analyze similarity for each base
    for base, crews in base_crews.items():
        if len(crews) < 2:
            print(f"Base {base} has fewer than 2 crews, skipping similarity analysis.")
            continue

        all_similarities = []
        for crew1, crew2 in combinations(crews, 2):
            flights1 = crew_flights.get(crew1, set())
            flights2 = crew_flights.get(crew2, set())

            if not flights1 and not flights2:
                similarity = 1.0  # Both have no qualifications
            elif not flights1 or not flights2:
                similarity = 0.0 # One has qualifications, the other doesn't
            else:
                intersection = len(flights1.intersection(flights2))
                union = len(flights1.union(flights2))
                similarity = intersection / union if union > 0 else 1.0
            
            all_similarities.append(similarity)

        if all_similarities:
            average_similarity = sum(all_similarities) / len(all_similarities)
            print(f"Base: {base}")
            print(f"  Number of crews: {len(crews)}")
            print(f"  Average flight qualification similarity: {average_similarity:.4f}")
            print("-" * 30)

if __name__ == "__main__":
    analyze_flight_qualifications('data/0606')
    print("\n" + "="*50 + "\n")
    analyze_flight_qualifications('data/0623')
    print("\n" + "="*50 + "\n")
    analyze_flight_qualifications('data/0703') 