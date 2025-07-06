import pandas as pd
from datetime import datetime, timedelta

# ------------------
# 1. DATA STRUCTURES
# ------------------

class Flight:
    """Represents a single flight leg."""
    def __init__(self, id, dep_airport, arr_airport, std, sta, fleet, aircraft_no, fly_time):
        self.id = id
        self.dep_airport = dep_airport
        self.arr_airport = arr_airport
        self.std = pd.to_datetime(std)  # departure time
        self.sta = pd.to_datetime(sta)  # arrival time
        self.fleet = fleet
        self.aircraft_no = aircraft_no
        self.fly_time = float(fly_time) / 60.0  # Convert to hours

    def __repr__(self):
        return f"Flight({self.id}, {self.dep_airport}->{self.arr_airport}, {self.std})"

class GroundTask:
    """Represents a ground duty or a bus positioning task."""
    def __init__(self, id, task_type, start_time, end_time, start_station, end_station=None):
        self.id = id
        self.task_type = task_type # e.g., 'BUS' or other ground duties
        self.start_time = pd.to_datetime(start_time)
        self.end_time = pd.to_datetime(end_time)
        self.start_station = start_station
        self.end_station = end_station if end_station is not None else start_station # If no end station, it's the same as start

    def __repr__(self):
        if self.start_station == self.end_station:
            return f"GroundTask({self.id}, {self.task_type} at {self.start_station})"
        return f"GroundTask({self.id}, {self.task_type}, {self.start_station}->{self.end_station})"


class DutyPeriod:
    """Represents a single flight duty period (FDP)."""
    def __init__(self, start_time, start_airport):
        self.tasks = []
        self.start_time = start_time
        self.end_time = None
        self.start_airport = start_airport
        self.end_airport = None
        self.flight_time = 0
        self.flight_duty_time = 0

    def add_task(self, task):
        """Adds a task (flight or ground) to the duty period and updates metrics."""
        self.tasks.append(task)
        if isinstance(task, Flight):
            self.flight_time += task.fly_time
            # Duty ends at the arrival time of the *last flight* in the FDP
            self.end_time = task.sta 
            self.end_airport = task.arr_airport
        else: # GroundTask
            self.end_time = task.end_time
            self.end_airport = task.end_station

        self.flight_duty_time = (self.end_time - self.start_time).total_seconds() / 3600.0

    @property
    def num_flights(self):
        return len([t for t in self.tasks if isinstance(t, Flight)])
    
    @property
    def num_tasks(self):
        return len(self.tasks)
    

class Crew:
    """Represents a crew member."""
    def __init__(self, id, base, initial_station):
        self.id = id
        self.base = base
        self.initial_station = initial_station
        self.qualified_flights = set()
        self.schedule = [] # List of assigned tasks (Flights or GroundTasks)
        self.duty_periods = [] # List of DutyPeriod objects
        
        # Dynamic state for scheduling
        self.current_station = initial_station
        # The start of the planning period, can be considered as end of a long rest
        self.available_time = datetime(2025, 5, 28, 0, 0) 

    def __repr__(self):
        return f"Crew({self.id}, Base: {self.base})"
    

# ------------------
# 3. RULES VALIDATOR
# ------------------
class Rules:
    """A class to encapsulate all the scheduling rules."""

    MIN_REST_HOURS = 12
    MAX_DUTY_HOURS = 12
    MAX_FLIGHTS_PER_DUTY = 4
    MAX_TASKS_PER_DUTY = 6
    MAX_FLIGHT_HOURS_PER_DUTY = 8
    MIN_CONNECTION_HOURS_DIFF_AIRCRAFT = 3
    MIN_CONNECTION_HOURS_BUS = 2

    @staticmethod
    def get_crew_state(crew):
        """Gets the current state of a crew member from their schedule."""
        if not crew.schedule:
            # At the start of the planning period, available at their initial station
            return crew.initial_station, datetime(2025, 5, 29, 0, 0), True
        
        last_task = crew.schedule[-1]
        
        # Determine end station based on task type
        end_station = last_task.end_station if isinstance(last_task, GroundTask) else last_task.arr_airport
        
        # Determine end time based on task type
        end_time = last_task.end_time if isinstance(last_task, GroundTask) else last_task.sta

        is_rested = True # This is a placeholder and needs a much more detailed implementation
                         # based on Rule #8 (flight cycles) and Rule #7 (minimum rest).
        
        return end_station, end_time, is_rested

    @staticmethod
    def can_start_new_duty(crew, flight, layover_stations):
        """Check if a crew can start a new duty period with this flight."""
        # Rule 10: Qualification
        if flight.id not in crew.qualified_flights:
            return False
            
        current_station, available_time, is_rested = Rules.get_crew_state(crew)

        # Rule 2: Location Connection (must be at a layover station to start a new duty)
        # For now, we simplify and don't implement positioning. The crew must already be there.
        if current_station != flight.dep_airport:
            return False
        
        # The start location for a duty must be a layover station
        if flight.dep_airport not in layover_stations:
            return False

        # Rule 7: Minimum Rest Time
        if (flight.std - available_time).total_seconds() / 3600 < Rules.MIN_REST_HOURS:
            return False

        # Additional check: The new duty itself must be valid
        if (flight.sta - flight.std).total_seconds() / 3600 > Rules.MAX_DUTY_HOURS:
            return False
        if flight.fly_time > Rules.MAX_FLIGHT_HOURS_PER_DUTY:
            return False
        
        # Rules 8 (cycles) and 9 (total time) are harder and will be checked elsewhere for now.
        
        return True # If all checks pass

    @staticmethod
    def can_extend_duty(crew, flight, layover_stations):
        """Check if a crew can extend their current duty period with this flight."""
        if not crew.duty_periods:
            return False # No duty period to extend

        last_duty = crew.duty_periods[-1]
        last_task = last_duty.tasks[-1]

        # Rule: Duty must end at a layover station
        if flight.arr_airport not in layover_stations:
            return False

        # Rule 4: Task quantity limits
        if last_duty.num_flights + 1 > Rules.MAX_FLIGHTS_PER_DUTY:
            return False
        if last_duty.num_tasks + 1 > Rules.MAX_TASKS_PER_DUTY:
            return False
        
        # Rule 2: Location Connection
        prev_airport = last_task.end_station if isinstance(last_task, GroundTask) else last_task.arr_airport
        if prev_airport != flight.dep_airport:
            return False

        # Rule 3: Minimum Connection Time
        # isinstance() checks if last_task is a GroundTask object
        # If it is, use end_time attribute, otherwise use sta (scheduled time of arrival)
        prev_end_time = last_task.end_time if isinstance(last_task, GroundTask) else last_task.sta
        connection_hours = (flight.std - prev_end_time).total_seconds() / 3600
        
        # Check if previous task was a bus
        if isinstance(last_task, GroundTask) and last_task.task_type == 'BUS':
            if connection_hours < Rules.MIN_CONNECTION_HOURS_BUS:
                return False
        # Check if previous task was a flight with a different tail number
        elif isinstance(last_task, Flight) and last_task.aircraft_no != flight.aircraft_no:
            if connection_hours < Rules.MIN_CONNECTION_HOURS_DIFF_AIRCRAFT:
                return False
        # Simplified: for same tail number, there's a minimal connection time of 3 h.
        # TODO: decide whether assign the flight to be a positioning flight(DDH) or flight mission.
        elif connection_hours < 3: 
            return False

        # Rule 5 & 6: Duty time limits
        new_flight_time = last_duty.flight_time + flight.fly_time
        new_duty_hours = (flight.sta - last_duty.start_time).total_seconds() / 3600

        if new_flight_time > Rules.MAX_FLIGHT_HOURS_PER_DUTY:
            return False
        if new_duty_hours > Rules.MAX_DUTY_HOURS:
            return False

        return True

    @staticmethod
    def find_valid_positioning(crew, flight_to_catch, layover_stations, positioning_options):
        """Finds the first available valid positioning task."""
        current_station, available_time, _ = Rules.get_crew_state(crew)
        
        if current_station == flight_to_catch.dep_airport:
            return None # Already at the right place
        
        # Lookup potential positioning tasks
        potential_tasks = positioning_options.get((current_station, flight_to_catch.dep_airport), [])
        
        for task in potential_tasks:
            task_start = task.std if isinstance(task, Flight) else task.start_time
            task_end = task.sta if isinstance(task, Flight) else task.end_time
            
            # Crew must be available and rested for the positioning task
            if (task_start - available_time).total_seconds() / 3600 < Rules.MIN_REST_HOURS:
                continue

            # Check connection time
            connection_hours = (flight_to_catch.std - task_end).total_seconds() / 3600
            if isinstance(task, Flight): # Positioning via another flight
                 if connection_hours < Rules.MIN_CONNECTION_HOURS_DIFF_AIRCRAFT: # Assume different aircraft for simplicity
                     continue
            else: # Positioning via Bus
                if connection_hours < Rules.MIN_CONNECTION_HOURS_BUS:
                    continue
            
            # Check if the combined duty period is valid
            duty_start_time = task_start
            duty_end_time = flight_to_catch.sta
            total_duty_hours = (duty_end_time - duty_start_time).total_seconds() / 3600
            
            if total_duty_hours > Rules.MAX_DUTY_HOURS:
                continue

            total_flight_hours = flight_to_catch.fly_time
            if isinstance(task, Flight):
                total_flight_hours += task.fly_time
            
            if total_flight_hours > Rules.MAX_FLIGHT_HOURS_PER_DUTY:
                continue

            # Found a valid positioning task
            return task
        
        return None # No valid positioning found


# ------------------
# 4. HEURISTIC SOLUTION
# ------------------

def preprocess_positioning_options(flights, buses):
    """Create a lookup for positioning tasks."""
    print("Preprocessing positioning options...")
    positioning_options = {}
    
    all_tasks = list(flights.values()) + list(buses.values())
    
    for task in all_tasks:
        dep = task.dep_airport if isinstance(task, Flight) else task.start_station
        arr = task.arr_airport if isinstance(task, Flight) else task.end_station
        if (dep, arr) not in positioning_options:
            positioning_options[(dep, arr)] = []
        positioning_options[(dep, arr)].append(task)
    
    # Sort tasks by start time for efficient searching
    for key in positioning_options:
        positioning_options[key].sort(key=lambda t: t.std if isinstance(t, Flight) else t.start_time)
        
    print(f"Found positioning options between {len(positioning_options)} airport pairs.")
    return positioning_options

def construct_initial_solution(crews, flights, ground_duties, buses, layover_stations):
    """Constructs an initial feasible solution using a greedy heuristic."""
    print("\nConstructing initial solution...")

    # Pre-process positioning options
    positioning_options = preprocess_positioning_options(flights, buses)

    # 1. Pre-assign ground duties (already done in main, but let's be explicit)
    # The logic for pre-assigning needs to be robust, for now we assume they are chronological
    
    # 2. Sort flights chronologically
    sorted_flights = sorted(list(flights.values()), key=lambda f: f.std)
    
    unassigned_flights = []

    # 3. Iterate through sorted flights and assign crews
    for i, flight in enumerate(sorted_flights):
        if (i+1) % 10 == 0: # Print progress update every 10 flights
            print(f"\rAssigning flight {i+1}/{len(sorted_flights)}...", end="")
        
        best_candidate, best_score, assignment_type, pos_task = find_best_candidate(flight, crews, layover_stations, positioning_options)
        
        if best_candidate:
            assign_flight_to_crew(best_candidate, flight, layover_stations, assignment_type, pos_task)
        else:
            unassigned_flights.append(flight)
    
    print(f"\nFinished construction. Unassigned flights: {len(unassigned_flights)}")
    
    final_roster = generate_roster_output(crews)
    return final_roster, unassigned_flights

def find_best_candidate(flight, crews, layover_stations, positioning_options):
    """Finds the best-scoring valid crew for a given flight."""
    valid_candidates = []
    for crew in crews.values():
        # Possibility 1: Extend an existing duty period
        if Rules.can_extend_duty(crew, flight, layover_stations):
            score = calculate_score(crew, flight, is_new_duty=False, positioning_task=None)
            valid_candidates.append((crew, score, "extend", None))

        # Possibility 2: Start a new duty period directly
        if Rules.can_start_new_duty(crew, flight, layover_stations):
            score = calculate_score(crew, flight, is_new_duty=True, positioning_task=None)
            valid_candidates.append((crew, score, "start", None))   
        
        # Possibility 3: Start a new duty with positioning
        pos_task = Rules.find_valid_positioning(crew, flight, layover_stations, positioning_options)
        if pos_task:
            score = calculate_score(crew, flight, is_new_duty=True, positioning_task=pos_task)
            valid_candidates.append((crew, score, "start_pos", pos_task))

    if not valid_candidates:
        return None, float('inf'), None, None

    # Return the candidate with the best (lowest) score
    best_crew, best_score, assignment_type, pos_task = min(valid_candidates, key=lambda x: x[1])
    return best_crew, best_score, assignment_type, pos_task


def assign_flight_to_crew(crew, flight, layover_stations, assignment_type, positioning_task=None):
    """Assigns the flight to the crew and updates the crew's state."""
    
    if assignment_type == "start":
        new_duty_period = DutyPeriod(start_time=flight.std, start_airport=flight.dep_airport)
        new_duty_period.add_task(flight)
        crew.duty_periods.append(new_duty_period)
        crew.schedule.append(flight)
    elif assignment_type == "extend":
        crew.duty_periods[-1].add_task(flight)
        crew.schedule.append(flight)
    elif assignment_type == "start_pos":
        # Mark the positioning task
        positioning_task.is_positioning = True
        
        # Duty starts with the positioning task
        pos_start_time = positioning_task.std if isinstance(positioning_task, Flight) else positioning_task.start_time
        pos_start_airport = positioning_task.dep_airport if isinstance(positioning_task, Flight) else positioning_task.start_station
        
        new_duty_period = DutyPeriod(start_time=pos_start_time, start_airport=pos_start_airport)
        new_duty_period.add_task(positioning_task)
        new_duty_period.add_task(flight) # Then add the actual flight
        
        crew.duty_periods.append(new_duty_period)
        crew.schedule.append(positioning_task)
        crew.schedule.append(flight)


def calculate_score(crew, flight, is_new_duty, positioning_task):
    """Calculates a heuristic score for a potential assignment."""
    score = 0
    
    # Base score is idle time of the crew
    _, available_time, _ = Rules.get_crew_state(crew)
    idle_time_hours = (flight.std - available_time).total_seconds() / 3600
    score += idle_time_hours

    # Penalty for an overnight stay at a non-base station (proxy for layover cost)
    if flight.arr_airport != crew.base and flight.sta.date() > flight.std.date():
        score += 24 # Penalize equivalent to 24 hours of idle time

    # Penalty for starting a new duty, to encourage extending existing duties.
    if is_new_duty:
        score += 24 # Penalize equivalent to 24 hours of idle time
    
    # Penalty for using a positioning task (proxy for positioning cost)
    if positioning_task:
        score += 12 # Penalize equivalent to 12 hours of idle time
        
    return score

def generate_roster_output(crews):
    """Generates the final submission file from the crew schedules."""
    output = [['crewId', 'taskId', 'isDDH']]
    for crew_id, crew in crews.items():
        # Add a flag for positioning tasks if not already present
        for task in crew.schedule:
            if not hasattr(task, 'is_positioning'):
                 task.is_positioning = False

        for task in crew.schedule:
            is_ddh = 1 if (isinstance(task, Flight) and task.is_positioning) or \
                          (isinstance(task, GroundTask) and task.task_type == 'BUS') else 0
            output.append([crew.id, task.id, is_ddh])
    return output


# ------------------
# 2. DATA LOADING
# ------------------

def load_all_data(data_path="./data"):
    """Loads all necessary data from CSV files."""
    print("Loading data...")

    # Load flights
    flight_df = pd.read_csv(f"{data_path}/flight.csv")
    flights = {
        row['id']: Flight(
            row['id'], row['depaAirport'], row['arriAirport'], # <--- 修改后
            row['std'], row['sta'], row['fleet'],
            row['aircraftNo'], row['flyTime']
        ) for _, row in flight_df.iterrows()
    }

    # Load crew
    crew_df = pd.read_csv(f"{data_path}/crew.csv")
    crews = {
        row['crewId']: Crew(        
            row['crewId'], row['base'], row['stayStation'] 
        ) for _, row in crew_df.iterrows()
    }

    # Load crew qualifications
    qual_df = pd.read_csv(f"{data_path}/crewLegMatch.csv")
    for _, row in qual_df.iterrows():
        if row['crewId'] in crews:
            crews[row['crewId']].qualified_flights.add(row['legId'])
    
    # Load layover stations
    layover_stations = set(pd.read_csv(f"{data_path}/layoverStation.csv")['airport'])

    # Load bus info (as GroundTask)
    bus_df = pd.read_csv(f"{data_path}/busInfo.csv")
    buses = {
        row['id']: GroundTask(
            row['id'], 'BUS', row['td'], row['ta'],
            row['depaAirport'], row['arriAirport']
        ) for _, row in bus_df.iterrows()
    }
    
    # Load groundDuty.csv as well
    ground_duty_df = pd.read_csv(f"{data_path}/groundDuty.csv")
    # This part needs to be handled differently. These are not tasks to be assigned,
    # but pre-assigned duties for specific crew members.
    # We will process them later when building the initial solution.
    ground_duties = {}
    for _, row in ground_duty_df.iterrows():
        # We can store them by crewId for easier access
        if row['crewId'] not in ground_duties:
            ground_duties[row['crewId']] = []
        ground_duties[row['crewId']].append(
            GroundTask(
                id=row['id'],
                task_type='DUTY', # We'll call it 'DUTY'
                start_time=row['startTime'],
                end_time=row['endTime'],
                start_station=row['airport']
                # end_station is handled by the default in the constructor
            )
        )

    print(f"Loaded {len(flights)} flights, {len(crews)} crews, {len(buses)} bus routes, and parsed {len(ground_duty_df)} ground duty entries.")    
    return flights, crews, buses, ground_duties, layover_stations


    
if __name__ == '__main__':
    # Check for pandas and install if not present
    try:
        import pandas as pd
    except ImportError:
        print("Pandas not found. Please install it using: pip install pandas")
        exit()
        
    flights, crews, buses, ground_duties, layover_stations = load_all_data()
    
    # --- Add a flag for positioning flights ---
    for flight in flights.values():
        flight.is_positioning = False

    # --- Start scheduling process ---
    final_roster, unassigned_flights = construct_initial_solution(crews, flights, ground_duties, buses, layover_stations)

    # --- Write output to CSV ---
    pd.DataFrame(final_roster[1:], columns=final_roster[0]).to_csv("rosterResult.csv", index=False)
    print("\nGenerated rosterResult.csv")




# if __name__ == '__main__':
#     # Check for pandas and install if not present
#     try:
#         import pandas as pd
#     except ImportError:
#         print("Pandas not found. Please install it using: pip install pandas")
#         exit()
        
#     flights, crews, buses, ground_duties, layover_stations = load_all_data()

#     # --- Print some samples to verify ---
#     print("\n--- Sample Data ---")
#     print("First flight:", list(flights.values())[0])
#     first_crew_id = list(crews.keys())[0]
#     print("First crew:", crews[first_crew_id])
#     print(f"Qualifications for {first_crew_id}: {len(crews[first_crew_id].qualified_flights)} flights")
#     print("First bus:", list(buses.values())[0])
#     print("First ground duty:", list(ground_duties.values())[0])
#     print(f"Total layover stations: {len(layover_stations)}")