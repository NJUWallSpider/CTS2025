import re

def analyze_schedule_report(report_path):
    """
    Analyzes a schedule report to calculate the average number of flights
    per Flight Duty Period (FDP).

    Args:
        report_path (str): The path to the schedule report file.
    """
    fdp_count = 0
    total_flights_in_fdps = 0
    in_duty_period = False
    current_duty_flights = 0
    is_flight_duty = False

    try:
        with open(report_path, 'r', encoding='utf-8') as f:
            for line in f:
                if "=== Duty Period" in line:
                    if in_duty_period and is_flight_duty:
                        fdp_count += 1
                        total_flights_in_fdps += current_duty_flights
                    
                    in_duty_period = True
                    is_flight_duty = False
                    current_duty_flights = 0
                
                elif "--------------------------------------------------" in line and in_duty_period:
                    if is_flight_duty:
                        fdp_count += 1
                        total_flights_in_fdps += current_duty_flights
                    in_duty_period = False
                    is_flight_duty = False
                    current_duty_flights = 0

                if in_duty_period:
                    if "Flights: " in line:
                        flight_count_match = re.search(r'Flights: (\d+)', line)
                        if flight_count_match:
                            num_flights = int(flight_count_match.group(1))
                            if num_flights > 0:
                                is_flight_duty = True
                                current_duty_flights = num_flights

        if in_duty_period and is_flight_duty:
            fdp_count += 1
            total_flights_in_fdps += current_duty_flights

        if fdp_count > 0:
            average_flights_per_fdp = total_flights_in_fdps / fdp_count
            print(f"Analyzed report: {report_path}")
            print(f"Total Flight Duty Periods (FDPs): {fdp_count}")
            print(f"Total Flights in FDPs: {total_flights_in_fdps}")
            print(f"Average Flights per FDP: {average_flights_per_fdp:.2f}")
        else:
            print("No Flight Duty Periods found in the report.")

    except FileNotFoundError:
        print(f"Error: Could not find the report file at {report_path}")
    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    analyze_schedule_report('heuristic/report/0606/schedule_report.txt') 