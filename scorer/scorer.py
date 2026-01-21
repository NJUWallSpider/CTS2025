import pandas as pd
from datetime import datetime, timedelta

# --- Configuration ---
# Planning period start and end dates
PLAN_START_DATE = datetime(2025, 4, 29).date()
PLAN_END_DATE = datetime(2025, 5, 4).date()

# --- Helper Function: Load and Preprocess Data ---
def load_data(data_path):
    """Loads all necessary CSV files and performs initial processing."""
    try:
        # Task info
        flights_df = pd.read_csv(data_path + 'flight.csv', dtype={'aircraftNo': str})
        bus_df = pd.read_csv(data_path + 'busInfo.csv')
        crew_df = pd.read_csv(data_path + 'crew.csv').set_index('crewId')
        layover_stations_df = pd.read_csv(data_path +'layoverStation.csv')
        allowed_layover_stations = set(layover_stations_df['airport'])
        
        # Time format conversion
        flights_df['std'] = pd.to_datetime(flights_df['std'])
        flights_df['sta'] = pd.to_datetime(flights_df['sta'])
        bus_df['td'] = pd.to_datetime(bus_df['td'])
        bus_df['ta'] = pd.to_datetime(bus_df['ta'])
        
        # Flight and Bus tasks (From competition base data)
        flights_df.rename(columns={'id': 'taskId', 'std': 'startTime', 'sta': 'endTime', 'flyTime': 'flightMinutes'}, inplace=True)
        bus_df.rename(columns={'id': 'taskId', 'td': 'startTime', 'ta': 'endTime'}, inplace=True)
        bus_df['flightMinutes'] = 0 # Bus has no flight time
        bus_df['fleet'] = 'BUS'
        bus_df['aircraftNo'] = 'BUS'
        
        # Merge flight and bus info
        base_tasks_df = pd.concat([flights_df, bus_df], sort=False)
        base_tasks_df['startTime'] = pd.to_datetime(base_tasks_df['startTime'])
        base_tasks_df['endTime'] = pd.to_datetime(base_tasks_df['endTime'])

        # Ground duties (New data source)
        ground_duty_df = pd.read_csv(data_path + 'groundDuty.csv')
        ground_duty_df.rename(columns={'id': 'taskId'}, inplace=True)
        ground_duty_df['startTime'] = pd.to_datetime(ground_duty_df['startTime'])
        ground_duty_df['endTime'] = pd.to_datetime(ground_duty_df['endTime'])
        # For ground duties, departure and arrival airports are the same
        ground_duty_df['depaAirport'] = ground_duty_df['airport']
        ground_duty_df['arriAirport'] = ground_duty_df['airport']

        # Qualification info
        crew_leg_match_df = pd.read_csv(data_path + 'crewLegMatch.csv')
        # Create a set of (crewId, legId) for fast lookup
        valid_pairings = set(zip(crew_leg_match_df['crewId'], crew_leg_match_df['legId']))

        return base_tasks_df, ground_duty_df, crew_df, valid_pairings, allowed_layover_stations, flights_df

    except FileNotFoundError as e:
        print(f"Error: Data file {e.filename} not found. Please ensure all CSV files are in the same directory.")
        return None, None, None, None, None, None

def split_duty_periods(tasks_list, violation_counts):
    if not tasks_list: 
        return [], violation_counts

    all_duty_days = []
    current_duty_day = []
    i = 0
    while i < len(tasks_list):
        task_current = tasks_list[i]

        # --- 1. Handle ground duty ---
        # isDuty=0 task is rest, it ends the previous duty day, and itself does not belong to any duty day
        # if task_current['taskId'].startswith('grd'):
        # # if task_current['isDuty'] == 0:
        #     if current_duty_day:
        #         all_duty_days.append(current_duty_day)
        #         current_duty_day = []
        #     i += 1
        #     continue

        # --- 1. Determine whether to start a new duty day ---
        # If it is the first task, or the rest time from the previous task is long enough, start a new duty day
        if not current_duty_day: # Start of a new day
            current_duty_day.append(task_current)
        else: # Not a new day, check relationship with previous task
            task_prev = tasks_list[i-1]
            rest_time = task_current['startTime'] - task_prev['endTime']
            if rest_time >= timedelta(hours=12):
                # Rest is long enough, end the old one, start a new one
                all_duty_days.append(current_duty_day)
                current_duty_day = [task_current]
            else:
                # Rest time is insufficient, add to current duty day
                current_duty_day.append(task_current)

        # If the previous task of the duty day is a ground duty, check if the rest time is violated
        if i > 0:
            if tasks_list[i-1]['taskId'].startswith('grd') and tasks_list[i-1]['isDuty'] == 1 and len(current_duty_day) == 1:
                rest_to_next = task_current['startTime'] - tasks_list[i-1]['endTime']
                if rest_to_next < timedelta(hours=12):
                    violation_counts['飞行值勤日最小休息时间限制'] += 1      


       # --- 2. Check if the current duty day exceeds capacity ---
        duty_df = pd.DataFrame(current_duty_day)
        flights_in_duty = duty_df[duty_df['taskId'].str.startswith('Flt')]

        force_break = False

        # # Checkpoint a: Number of flight tasks > 4
        # if len(flights_in_duty) > 4: force_break = True
        # # Checkpoint b: Total work tasks > 6
        # if len(work_tasks_df) > 6: force_break = True
        # # Checkpoint c: Flight time > 8 hours
        # if flights_in_duty['flightMinutes'].sum() / 60 > 8: force_break = True
        # Checkpoint d: Flight duty period > 12 hours (Note: end time is still calculated based on the last flight task)
        if not flights_in_duty.empty:
            # Duty start time is the start time of the first task in the current duty_day
            duty_start_time = duty_df['startTime'].iloc[0]
            # Duty end time is the arrival time of the last "flight" task in the current duty_day
            duty_end_time = flights_in_duty['endTime'].iloc[-1]
            if (duty_end_time - duty_start_time) > timedelta(hours=12):
                force_break = True
        
        # --- 4. If limit exceeded, interrupt the duty day and check subsequent rest ---
        is_not_last_task = (i + 1 < len(tasks_list))
        if force_break:
            all_duty_days.append(current_duty_day)
            current_duty_day = []
            
            # Check if rest with the next task is violated due to this
            if is_not_last_task:
                rest_to_next = tasks_list[i+1]['startTime'] - task_current['endTime']
                if rest_to_next < timedelta(hours=12) and not tasks_list[i+1]['taskId'].startswith('grd'):
                    violation_counts['飞行值勤日最小休息时间限制'] += 1
        
        i += 1

    if current_duty_day:
        all_duty_days.append(current_duty_day)
    
    for duty_day in all_duty_days:
        has_no_filght = True
        for task in duty_day:
            if task['taskId'].startswith('Flt'):
                has_no_filght = False
                break
        if has_no_filght:
            all_duty_days.remove(duty_day)
    return all_duty_days, violation_counts

    
def check_flight_cycles(tasks_list, crew_base):
    """
    Check the "4-on-2-off" rule based on the user-defined "reverse cycle" algorithm.
    This algorithm better handles the constraint that "the end of a cycle must be a flight".

    Args:
        tasks_list (list): All tasks of a single crew sorted by time.
        crew_base (str): The base of the crew.

    """
    violation_counts = 0
    if not tasks_list:
        return

    all_cycles = []
    current_cycle_tasks = []
    
    # --- Step 1: Traverse backwards to identify all flight cycles ---
    i = len(tasks_list) - 1
    while i >= 0:
        task_current = tasks_list[i]
        
        # If current cycle is empty, we need to find a flight task to "start" building a new cycle
        if not current_cycle_tasks:
            # According to rules, only flight tasks can be the end of a cycle
            if task_current['taskId'].startswith('Flt'):
                current_cycle_tasks.insert(0, task_current)
        else:
            # If cycle building has started, check interval with the next task
            task_next_in_sequence = current_cycle_tasks[0] # The first task of the current cycle
            
            rest_start_time = task_current['endTime']
            rest_end_time = task_next_in_sequence['startTime']
            rest_location = task_current['arriAirport']

            # Calculate full calendar days of rest
            full_rest_days = (rest_end_time.date() - rest_start_time.date()).days - 1
            
            # Determine if it is a "cycle end" long rest (must be at base)
            if full_rest_days >= 2:
                # Found long rest, meaning current cycle ends here (excluding current task)
                all_cycles.append(current_cycle_tasks)
                # Reset for the next cycle
                current_cycle_tasks = []
                
                # Reconsider current task, see if it can start a new cycle
                if task_current['taskId'].startswith('Flt'):
                    current_cycle_tasks.insert(0, task_current)
            else:
                # Rest time insufficient, cycle continues extending, add current task to the front of the cycle
                current_cycle_tasks.insert(0, task_current)

        i -= 1

    # After loop ends, if there are still unsaved cycles (usually the first cycle), add to list
    if current_cycle_tasks:
        all_cycles.append(current_cycle_tasks)

    # --- Step 2: Check if the span of each identified flight cycle exceeds limit ---
    for cycle in all_cycles:
        if not cycle:
            continue
            
        # According to rules, cycle must contain flight tasks. This check can be omitted as construction guarantees it.
        has_flight = any(task['taskId'].startswith('Flt') for task in cycle)
        if not has_flight:
            continue
            
        cycle_start_date = cycle[0]['startTime'].date()
        cycle_end_date = cycle[-1]['endTime'].date()
        
        cycle_span_days = (cycle_end_date - cycle_start_date).days + 1
        
        if cycle_span_days > 4:
            violation_counts += 1
    return violation_counts

def check_flight_cycles_final_logic(tasks_list, crew_base):
    """
    Check "4-on-2-off" rule based on user-requested "forward cycle" division logic.
    Accumulate from the first task, start a new cycle when task interval with previous task is >= 2 full calendar days.
    If cycle span exceeds 4 calendar days, record a violation.

    Args:
        tasks_list (list): All tasks of a single crew sorted by time.
        crew_base (str): The base of the crew.
    """
    violation_counts = 0
    if not tasks_list or len(tasks_list) == 0:
        return 0

    all_cycles = []
    current_cycle = [tasks_list[0]]  # Start from the first task
    
    # Traverse from the second task
    for i in range(1, len(tasks_list)):
        task_current = tasks_list[i]
        task_prev = tasks_list[i-1]
        
        # Calculate interval days with previous task
        rest_start_time = task_prev['endTime']
        rest_end_time = task_current['startTime']
        # Calculate full calendar days of rest
        full_rest_days = (rest_end_time.date() - rest_start_time.date()).days - 1
        
        if full_rest_days >= 2:
            # Rest days >= 2 days, end current cycle, start new cycle
            all_cycles.append(current_cycle)
            current_cycle = [task_current]
        else:
            # Rest days insufficient, continue current cycle
            current_cycle.append(task_current)
    
    # Add the last cycle
    if current_cycle:
        all_cycles.append(current_cycle)
    
    # Check if each cycle span exceeds 4 days
    for cycle in all_cycles:
        if len(cycle) > 0:
            cycle_start_date = cycle[0]['startTime'].date()
            cycle_end_date = cycle[-1]['endTime'].date()
            cycle_span_days = (cycle_end_date - cycle_start_date).days + 1
            
            if cycle_span_days > 4:
                violation_counts += 1
    
    return violation_counts
    
# --- Main Scoring Function ---
def calculate_score(data_path, submission_path):
    """
    Calculate total score based on given submission.csv file and output in specified format.
    """
    tasks_df, ground_duty_df, crew_df, valid_pairings, allowed_layover_stations, flights_df = load_data(data_path)
    if tasks_df is None: return

    try:
        submission_df = pd.read_csv(submission_path)
    except FileNotFoundError:
        print(f"Error: Submission file {submission_path} not found.")
        return

    # --- 1. Unify all task sources ---
    # Process submitted tasks (flight/deadhead)
    submitted_tasks = submission_df.merge(tasks_df, on='taskId', how='left')
    submitted_tasks['isDuty'] = 1 # Both flight and deadhead count as duty  

    # Merge all tasks into one DataFrame
    all_tasks_df = pd.concat([submitted_tasks, ground_duty_df], sort=False)
    all_tasks_df.fillna({'flightMinutes': 0}, inplace=True)

    # --- Initialize all metrics and violation counters ---
    scores = {
        "duty_day_avg_flight_time_score": 0,
        "uncovered_flights_penalty": 0,
        "new_layover_stations_penalty": 0,
        "out_of_base_layover_penalty": 0,
        "ddh_penalty": 0,
        "violations_penalty": 0
    }
    # <--- Logic modification: Use dictionary to record violations in detail ---
    violation_counts = {
        '任务重叠': 0,
        '多机长执飞': 0,
        '置位位置': 0,           # Rule 1
        '地点衔接规则': 0,       # Rule 2
        '连接时间': 0,           # Rule 3
        '飞行值勤日飞行任务数量限制': 0, # Rule 4a
        '飞行值勤日值勤任务数量限制': 0, # Rule 4b
        '飞行值勤日最大飞行时间限制': 0, # Rule 5
        '飞行值勤日最大飞行值勤时间限制': 0, # Rule 6
        '飞行值勤日最小休息时间限制': 0, # Rule 7 (Theoretically 0)
        '值四休二要求': 0,       # Rule 8
        '总飞行值勤时间限制': 0,     # Rule 9
        '资质检查': 0            # Rule 10
    }
    
    total_flight_minutes = 0
    total_duty_days = 0
    total_out_of_base_days = 0
    new_layover_stations = set()
    total_duty_calendar_days = 0

    # --- Global Check ---
    # Check: Multiple Captains
    task_assignments = submitted_tasks[submitted_tasks['taskId'].str.startswith('Flt') & submitted_tasks['isDDH'] == 0]['taskId'].value_counts()
    overlap = task_assignments[task_assignments > 1]
    violation_counts['多机长执飞'] = len(task_assignments[task_assignments > 1])

    # --- Calculate Basic Metrics ---
    all_flight_ids = set(flights_df['taskId'])
    submitted_flight_ids = set(submitted_tasks[submitted_tasks['isDDH'] == 0]['taskId'])
    uncovered_flights_count = len(all_flight_ids - submitted_flight_ids)
    ddh_count = submitted_tasks['isDDH'].sum()
    
    grouped_with_ground_duty = all_tasks_df.groupby('crewId')
    for crew_id, crew_schedule in grouped_with_ground_duty:
        # Check: Initial Location Connection (Rule 2)
        crew_schedule = crew_schedule.sort_values(by='startTime').reset_index(drop=True)
        crew_info = crew_df.loc[crew_id]
        initial_station = crew_info['stayStation']
        if crew_schedule['depaAirport'].iloc[0] != initial_station:
            violation_counts['地点衔接规则'] += 1
    
    # --- Detailed Check by Crew ---
    grouped = submitted_tasks.groupby('crewId')
    for crew_id, crew_schedule in grouped:
        if crew_schedule.empty: continue
        
        crew_info = crew_df.loc[crew_id]
        crew_base = crew_info['base']
        initial_station = crew_info['stayStation']
        
        crew_schedule = crew_schedule.sort_values(by='startTime').reset_index(drop=True)
        tasks_list = crew_schedule.to_dict('records')
        tasks_list_all = all_tasks_df[(all_tasks_df['crewId'] == crew_id) & (all_tasks_df['isDuty'] == 1)]\
                                .sort_values(by='startTime')\
                                .to_dict('records')
                          
        tasks_list_all_with_rest = all_tasks_df[(all_tasks_df['crewId'] == crew_id)]\
                                     .sort_values(by='startTime')\
                                     .to_dict('records')
        # Check: Qualification (Rule 10)
        for task in tasks_list:
            if (crew_id, task['taskId']) not in valid_pairings and task['taskId'].startswith('Flt'):
                violation_counts['资质检查'] += 1


        # Calculate: Overnight out of base (Case 2)
        if initial_station != crew_base:
            days = (tasks_list[0]['startTime'].date() - PLAN_START_DATE).days
            total_out_of_base_days += max(0, days)
        
        all_duty_days, violation_counts = split_duty_periods(tasks_list_all_with_rest, violation_counts)
        
        # Analyze each duty day
        crew_total_duty_minutes = 0
        for duty_day_tasks in all_duty_days:
            duty_day_df = pd.DataFrame(duty_day_tasks)
            
            duty_start_time = duty_day_df['startTime'].iloc[0]
            duty_end_time = duty_day_df['endTime'].iloc[-1]
            duty_day_flight_minutes = duty_day_df[duty_day_df['taskId'].str.startswith('Flt')]['flightMinutes'].sum()
            total_flight_minutes += duty_day_flight_minutes
            total_duty_calendar_days += (duty_end_time.date() - duty_start_time.date()).days + 1
            
            # Check: Deadhead Position (Rule 1)
            if len(duty_day_df) > 2:
                if duty_day_df.iloc[1:-1]['isDDH'].sum() > 0:
                     violation_counts['置位位置'] += duty_day_df.iloc[1:-1]['isDDH'].sum()

            # Check: Connection Time (Rule 3)
            for i in range(len(duty_day_df) - 1):
                t1, t2 = duty_day_df.iloc[i], duty_day_df.iloc[i+1]
                if t2['startTime'] < t1['endTime'] and not t1['taskId'].startswith('grd') and not t2['taskId'].startswith('grd'): 
                    violation_counts['任务重叠'] += 1
                if t1['taskId'].startswith('grd') or t2['taskId'].startswith('grd'):
                    continue
                conn_time = t2['startTime'] - t1['endTime']
                if t1['taskId'].startswith('ddh') or t2['taskId'].startswith('ddh'):
                    if conn_time < timedelta(hours=2): 
                        violation_counts['连接时间'] += 1
                elif 'aircraftNo' in t1 and 'aircraftNo' in t2 and t1['aircraftNo'] != t2['aircraftNo']:
                    if conn_time < timedelta(hours=3): 
                        violation_counts['连接时间'] += 1

            # Check: Task Count Limit (Rule 4)
            flight_tasks_count = duty_day_df[duty_day_df['taskId'].str.startswith('Flt')].shape[0]
            if flight_tasks_count > 4: violation_counts['飞行值勤日飞行任务数量限制'] += 1
            if len(duty_day_df) > 6: violation_counts['飞行值勤日值勤任务数量限制'] += 1
            
            # Check: Max Flight Time (Rule 5)
            if (duty_day_flight_minutes / 60) > 8: violation_counts['飞行值勤日最大飞行时间限制'] += 1

            # Check: Max Duty Time (Rule 6)
            flight_tasks_in_duty = duty_day_df[duty_day_df['taskId'].str.startswith('Flt')]
            if not flight_tasks_in_duty.empty:
                duration = flight_tasks_in_duty['endTime'].iloc[-1] - duty_day_df['startTime'].iloc[0]
                if duration > timedelta(hours=12): 
                    violation_counts['飞行值勤日最大飞行值勤时间限制'] += 1
                crew_total_duty_minutes += duration.total_seconds() / 60

        # Check: Total Duty Time (Rule 9)
        if (crew_total_duty_minutes / 60) > 60: violation_counts['总飞行值勤时间限制'] += 1

        for j in range(len(all_duty_days) - 1):
            prev_duty_df = pd.DataFrame(all_duty_days[j])
            next_duty_df = pd.DataFrame(all_duty_days[j+1])
            prev_duty_end_time = prev_duty_df['endTime'].iloc[-1]
            next_duty_start_time = next_duty_df['startTime'].iloc[0]
            rest_location = prev_duty_df['arriAirport'].iloc[-1]
            
            # --- Overnight out of base days: Case 1 and 3 (between duty days) ---
            # <--- Logic modification: Update logic here to match rules more clearly ---
            if rest_location != crew_base:
                days_diff = (next_duty_start_time.date() - prev_duty_end_time.date()).days
                if days_diff == 0:
                    # Case 3: Same day rest, count 1 day
                    total_out_of_base_days += 1
                else:
                    # Case 1: Cross midnight rest, count cross midnight days
                    total_out_of_base_days += days_diff
            # <--- Logic modification end ---

            # New layover station check
            if rest_location not in allowed_layover_stations and rest_location != initial_station:
                new_layover_stations.add(rest_location)
        
        violation_counts['值四休二要求'] += check_flight_cycles_final_logic(tasks_list_all, crew_base)
    
        for i in range(len(tasks_list_all_with_rest) - 1):
            t1, t2 = tasks_list_all_with_rest[i], tasks_list_all_with_rest[i+1]
            if t1['arriAirport'] != t2['depaAirport']: 
                violation_counts['地点衔接规则'] += 1

        final_station = crew_schedule.iloc[-1]['arriAirport']
        if final_station not in allowed_layover_stations and final_station != initial_station:
            new_layover_stations.add(final_station)

        # --- Overnight out of base days: Case 4 (Final stay) ---
        if final_station != crew_base:
            days = (PLAN_END_DATE - crew_schedule.iloc[-1]['endTime'].date()).days
            total_out_of_base_days += max(0, days)

    # --- Summary Score ---
    avg_flight_hours_per_duty_day = (total_flight_minutes / 60) / total_duty_calendar_days if total_duty_calendar_days > 0 else 0
    scores['duty_day_avg_flight_time_score'] = avg_flight_hours_per_duty_day * 1000
    scores['uncovered_flights_penalty'] = -5 * uncovered_flights_count
    scores['new_layover_stations_penalty'] = -10 * len(new_layover_stations)
    scores['out_of_base_layover_penalty'] = -0.5 * total_out_of_base_days
    scores['ddh_penalty'] = -0.5 * ddh_count
    total_violations = sum(violation_counts.values())
    scores['violations_penalty'] = -10 * total_violations
    total_score = sum(scores.values())

    # --- Output in specified format ---
    output_str = (
        f"Total Score: {total_score:.2f} "
        f"Avg Flight Hours/Duty Day: {avg_flight_hours_per_duty_day:.2f} "
        f"Uncovered Flights: {uncovered_flights_count} "
        f"Out of Base Overnight Days: {total_out_of_base_days} "
        f"New Layover Stations: {len(new_layover_stations)} "
        f"Deadhead Count: {ddh_count} "
        f"Task Overlap: {violation_counts['任务重叠']} "
        f"Multiple Captains: {violation_counts['多机长执飞']} "
        f"Deadhead Position: {violation_counts['置位位置']} "
        f"Location Connection: {violation_counts['地点衔接规则']} "
        f"Connection Time: {violation_counts['连接时间']} "
        f"Duty Flight Count Limit: {violation_counts['飞行值勤日飞行任务数量限制']} "
        f"Duty Task Count Limit: {violation_counts['飞行值勤日值勤任务数量限制']} "
        f"Duty Max Flight Time Limit: {violation_counts['飞行值勤日最大飞行时间限制']} "
        f"Duty Max Duty Time Limit: {violation_counts['飞行值勤日最大飞行值勤时间限制']} "
        f"Duty Min Rest Time Limit: {violation_counts['飞行值勤日最小休息时间限制']} "
        f"4-on-2-off Rule: {violation_counts['值四休二要求']} "
        f"Total Duty Time Limit: {violation_counts['总飞行值勤时间限制']} "
        f"Qualification Check: {violation_counts['资质检查']}"
    )
    print(output_str)


# --- Program Entry ---
if __name__ == '__main__':
    # Replace 'submission.csv' with your submission filename
    data_path = '/home/ubuntu/new-cts/data/0703/'
    submission_file = '/home/ubuntu/new-cts/result_0703.csv'
    calculate_score(data_path, submission_file)
