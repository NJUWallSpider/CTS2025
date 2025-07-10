import re
import pandas as pd

def find_unassigned_flights(report_path, flight_data_path, output_path):
    try:
        with open(report_path, 'r', encoding='utf-8') as f:
            report_content = f.read()
    except FileNotFoundError:
        print(f"Error: Report file not found at {report_path}")
        return

    assigned_flights = set(re.findall(r'Flight (Flt_\d+)', report_content))
    
    flights_df = pd.read_csv(flight_data_path)
    all_flights = set(flights_df['id'])
    
    unassigned_flights = sorted(list(all_flights - assigned_flights))
    
    with open(output_path, 'w') as f:
        for flight_id in unassigned_flights:
            f.write(f"{flight_id}\n")
            
    print(f"Found {len(unassigned_flights)} unassigned flights. List saved to {output_path}")

if __name__ == "__main__":
    report_file = 'heuristic/report/0703/schedule_report.txt'
    flight_file = 'data/0703/flight.csv'
    output_file = 'unassigned_flights.txt'
    find_unassigned_flights(report_file, flight_file, output_file) 