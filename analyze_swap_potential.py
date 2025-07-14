
import pandas as pd
import re
from collections import defaultdict

def analyze_potential_swaps(roster_path, schedule_report_path, crew_path, match_path):
    print("Loading data... (This may take a moment for crewLegMatch.csv)")
    try:
        roster_df = pd.read_csv(roster_path)
        crew_df = pd.read_csv(crew_path)
        match_df = pd.read_csv(match_path)
        with open(schedule_report_path, 'r', encoding='utf-8') as f:
            report_content = f.read()
    except FileNotFoundError as e:
        print(f"Error loading data: {e}")
        return

    # --- 1. Data Preparation ---

    # Get crew schedules from roster
    # Corrected 'flightId' to 'taskId'
    roster_df = roster_df[roster_df['isDDH'] == 0]
    crew_schedules = roster_df.groupby('crewId')['taskId'].apply(list).to_dict()

    # Get crew bases
    crew_bases = crew_df.set_index('crewId')['base'].to_dict()

    # Get crew qualifications
    print("Processing qualifications...")
    crew_qualifications = defaultdict(set)
    for _, row in match_df.iterrows():
        crew_qualifications[row['crewId']].add(row['legId'])

    # Get crew layovers from schedule report
    print("Parsing schedule report for layover information...")
    crew_layovers = defaultdict(list)
    crew_id_pattern = re.compile(r'Crew ID:\s+(Crew_\d+)')
    layover_pattern = re.compile(r'Layover Airport:\s+([A-Z]+)')
    
    # Split the report by crew sections
    sections = report_content.split('--------------------------------------------------')
    for section in sections:
        crew_id_match = crew_id_pattern.search(section)
        if crew_id_match:
            crew_id = crew_id_match.group(1)
            layovers = layover_pattern.findall(section)
            crew_layovers[crew_id] = layovers

    # --- 2. Analysis ---
    
    print("Analyzing potential swaps...")
    scheduled_crews = list(crew_schedules.keys())
    potential_swap_count = 0
    
    # Iterate through all unique pairs of crews
    for i in range(len(scheduled_crews)):
        for j in range(i + 1, len(scheduled_crews)):
            crew_a_id = scheduled_crews[i]
            crew_b_id = scheduled_crews[j]

            base_a = crew_bases.get(crew_a_id)
            base_b = crew_bases.get(crew_b_id)

            # Skip if crews have the same base or base info is missing
            if not base_a or not base_b or base_a == base_b:
                continue

            schedule_a = crew_schedules.get(crew_a_id, [])
            schedule_b = crew_schedules.get(crew_b_id, [])
            
            # Ensure both crews have schedules
            if not schedule_a or not schedule_b:
                continue

            # --- Check for beneficial swap for Crew A ---
            layovers_in_a_at_base_a = crew_layovers.get(crew_a_id, []).count(base_a)
            layovers_in_b_at_base_a = crew_layovers.get(crew_b_id, []).count(base_a)
            
            is_beneficial_for_a = layovers_in_b_at_base_a > layovers_in_a_at_base_a
            
            if is_beneficial_for_a:
                # Check if Crew A is qualified for all of Crew B's flights
                quals_a = crew_qualifications.get(crew_a_id, set())
                is_a_qualified_for_b = all(flight in quals_a for flight in schedule_b)
                
                if is_a_qualified_for_b:
                    potential_swap_count += 1
                    print(f"Found potential swap: Crew {crew_a_id} (Base: {base_a}) could take Crew {crew_b_id}'s schedule.")

            # --- Check for beneficial swap for Crew B ---
            layovers_in_b_at_base_b = crew_layovers.get(crew_b_id, []).count(base_b)
            layovers_in_a_at_base_b = crew_layovers.get(crew_a_id, []).count(base_b)
            
            is_beneficial_for_b = layovers_in_a_at_base_b > layovers_in_b_at_base_b
            
            if is_beneficial_for_b:
                # Check if Crew B is qualified for all of Crew A's flights
                quals_b = crew_qualifications.get(crew_b_id, set())
                is_b_qualified_for_a = all(flight in quals_b for flight in schedule_a)
                
                if is_b_qualified_for_a:
                    potential_swap_count += 1
                    print(f"Found potential swap: Crew {crew_b_id} (Base: {base_b}) could take Crew {crew_a_id}'s schedule.")


    print("\n--- Analysis Complete ---")
    print(f"Total number of potential beneficial swaps found: {potential_swap_count}")


if __name__ == "__main__":
    roster_file = 'Heuristic - best - reports/0711-4010,1468/rosterResult.csv'
    schedule_report_file = 'Heuristic - best - reports/0711-4010,1468/schedule_report.txt'
    crew_file = 'data/0711/crew.csv'
    match_file = 'data/0711/crewLegMatch.csv'
    analyze_potential_swaps(roster_file, schedule_report_file, crew_file, match_file) 