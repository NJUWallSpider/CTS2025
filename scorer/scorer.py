import pandas as pd
from datetime import datetime, timedelta

# --- 配置项 ---
# 计划期的开始和结束日期
PLAN_START_DATE = datetime(2025, 4, 29).date()
PLAN_END_DATE = datetime(2025, 5, 4).date()

# --- 辅助函数：加载和预处理数据 ---
def load_data(data_path):
    """加载所有必需的CSV文件并进行初步处理。"""
    try:
        # 任务信息
        flights_df = pd.read_csv(data_path + 'flight.csv', dtype={'aircraftNo': str})
        bus_df = pd.read_csv(data_path + 'busInfo.csv')
        crew_df = pd.read_csv(data_path + 'crew.csv').set_index('crewId')
        layover_stations_df = pd.read_csv(data_path +'layoverStation.csv')
        allowed_layover_stations = set(layover_stations_df['airport'])
        
        # 时间格式转换
        flights_df['std'] = pd.to_datetime(flights_df['std'])
        flights_df['sta'] = pd.to_datetime(flights_df['sta'])
        bus_df['td'] = pd.to_datetime(bus_df['td'])
        bus_df['ta'] = pd.to_datetime(bus_df['ta'])
        
        # 航班与大巴任务 (来自比赛基础数据)
        flights_df.rename(columns={'id': 'taskId', 'std': 'startTime', 'sta': 'endTime', 'flyTime': 'flightMinutes'}, inplace=True)
        bus_df.rename(columns={'id': 'taskId', 'td': 'startTime', 'ta': 'endTime'}, inplace=True)
        bus_df['flightMinutes'] = 0 # 大巴没有飞行时间
        bus_df['fleet'] = 'BUS'
        bus_df['aircraftNo'] = 'BUS'
        
        # 合并航班与大巴信息
        base_tasks_df = pd.concat([flights_df, bus_df], sort=False)
        base_tasks_df['startTime'] = pd.to_datetime(base_tasks_df['startTime'])
        base_tasks_df['endTime'] = pd.to_datetime(base_tasks_df['endTime'])

        # 占位任务 (新增数据源)
        ground_duty_df = pd.read_csv(data_path + 'groundDuty.csv')
        ground_duty_df.rename(columns={'id': 'taskId'}, inplace=True)
        ground_duty_df['startTime'] = pd.to_datetime(ground_duty_df['startTime'])
        ground_duty_df['endTime'] = pd.to_datetime(ground_duty_df['endTime'])
        # 对于占位任务，出发和到达地是同一个地方
        ground_duty_df['depaAirport'] = ground_duty_df['airport']
        ground_duty_df['arriAirport'] = ground_duty_df['airport']

        # 资质信息
        crew_leg_match_df = pd.read_csv(data_path + 'crewLegMatch.csv')
        # 创建一个 (crewId, legId) 的集合，用于快速查找
        valid_pairings = set(zip(crew_leg_match_df['crewId'], crew_leg_match_df['legId']))

        return base_tasks_df, ground_duty_df, crew_df, valid_pairings, allowed_layover_stations, flights_df

    except FileNotFoundError as e:
        print(f"错误：找不到数据文件 {e.filename}。请确保所有CSV文件都在同一目录下。")
        return None, None, None, None, None, None

def split_duty_periods(tasks_list, violation_counts):
    if not tasks_list: 
        return [], violation_counts

    all_duty_days = []
    current_duty_day = []
    i = 0
    while i < len(tasks_list):
        task_current = tasks_list[i]

        # --- 1. 处理占位任务 ---
        # isDuty=0 的任务是休息，它会结束之前的执勤日，并且自身不属于任何执勤日
        # if task_current['taskId'].startswith('grd'):
        # # if task_current['isDuty'] == 0:
        #     if current_duty_day:
        #         all_duty_days.append(current_duty_day)
        #         current_duty_day = []
        #     i += 1
        #     continue

        # --- 1. 判断是否开启新的执勤日 ---
        # 如果是第一个任务，或者与前一个任务休息时间足够长，则开启新执勤日
        if not current_duty_day: # 新一天的开始
            current_duty_day.append(task_current)
        else: # 不是新的一天，判断与前序任务的关系
            task_prev = tasks_list[i-1]
            rest_time = task_current['startTime'] - task_prev['endTime']
            if rest_time >= timedelta(hours=12):
                # 休息足够长，结束旧的，开启新的
                all_duty_days.append(current_duty_day)
                current_duty_day = [task_current]
            else:
                # 休息时间不足，加入当前执勤日
                current_duty_day.append(task_current)

        # 如果执勤日的上一个任务是执勤占位任务，检查休息时间是否违规
        if i > 0:
            if tasks_list[i-1]['taskId'].startswith('grd') and tasks_list[i-1]['isDuty'] == 1 and len(current_duty_day) == 1:
                rest_to_next = task_current['startTime'] - tasks_list[i-1]['endTime']
                if rest_to_next < timedelta(hours=12):
                    violation_counts['飞行值勤日最小休息时间限制'] += 1      


       # --- 2. 检查当前执勤日是否“容量超限” ---
        duty_df = pd.DataFrame(current_duty_day)
        flights_in_duty = duty_df[duty_df['taskId'].str.startswith('Flt')]

        force_break = False

        # # 检查点a: 飞行任务数 > 4
        # if len(flights_in_duty) > 4: force_break = True
        # # 检查点b: 总工作任务数 > 6
        # if len(work_tasks_df) > 6: force_break = True
        # # 检查点c: 飞行时间 > 8小时
        # if flights_in_duty['flightMinutes'].sum() / 60 > 8: force_break = True
        # 检查点d: 飞行值勤时间 > 12小时 (注意：结束时间仍按最后一个飞行任务算)
        if not flights_in_duty.empty:
            # 值勤开始时间是当前duty_day里第一个任务的开始时间
            duty_start_time = duty_df['startTime'].iloc[0]
            # 值勤结束时间是当前duty_day里最后一个“飞行”任务的到达时间
            duty_end_time = flights_in_duty['endTime'].iloc[-1]
            if (duty_end_time - duty_start_time) > timedelta(hours=12):
                force_break = True
        
        # --- 4. 如果超限，则中断执勤日并检查后续休息 ---
        is_not_last_task = (i + 1 < len(tasks_list))
        if force_break:
            all_duty_days.append(current_duty_day)
            current_duty_day = []
            
            # 检查与下一个任务的休息是否因此违规
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
    根据用户定义的“逆向循环”算法，检查“值四休二”规则。
    这个算法能更好地处理“周期末尾必须是飞行”的约束。

    Args:
        tasks_list (list): 按时间排序的、单个机组的所有任务。
        crew_base (str): 该机组的所属基地。

    """
    violation_counts = 0
    if not tasks_list:
        return

    all_cycles = []
    current_cycle_tasks = []
    
    # --- 步骤1：从后向前遍历，识别所有飞行周期 ---
    i = len(tasks_list) - 1
    while i >= 0:
        task_current = tasks_list[i]
        
        # 如果当前周期为空，我们需要找到一个飞行任务来“开启”一个新周期的构建
        if not current_cycle_tasks:
            # 根据规则，只有飞行任务可以作为周期的结尾
            if task_current['taskId'].startswith('Flt'):
                current_cycle_tasks.insert(0, task_current)
        else:
            # 如果周期已开始构建，检查与下一个任务的间隔
            task_next_in_sequence = current_cycle_tasks[0] # 当前周期的第一个任务
            
            rest_start_time = task_current['endTime']
            rest_end_time = task_next_in_sequence['startTime']
            rest_location = task_current['arriAirport']

            # 计算完整日历日休息天数
            full_rest_days = (rest_end_time.date() - rest_start_time.date()).days - 1
            
            # 判断是否为“周期结束”的长休息 (必须在基地)
            if full_rest_days >= 2:
                # 发现长休息，意味着当前周期到此结束（不包含当前任务）
                all_cycles.append(current_cycle_tasks)
                # 为下一个周期重置
                current_cycle_tasks = []
                
                # 重新考虑当前任务，看它是否能开启一个新的周期
                if task_current['taskId'].startswith('Flt'):
                    current_cycle_tasks.insert(0, task_current)
            else:
                # 休息时间不足，周期继续延伸，将当前任务加入周期的最前端
                current_cycle_tasks.insert(0, task_current)

        i -= 1

    # 循环结束后，如果仍有未保存的周期（通常是第一个周期），将其加入列表
    if current_cycle_tasks:
        all_cycles.append(current_cycle_tasks)

    # --- 步骤2：检查每个识别出的飞行周期的跨度是否超限 ---
    for cycle in all_cycles:
        if not cycle:
            continue
            
        # 根据规则，周期内必须包含飞行任务。此检查可省略，因为构建时已保证。
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
    根据用户要求的"正向周期"划分逻辑，检查"值四休二"规则。
    从第一个任务开始累加，当任务与上一个任务间隔大于等于两个完整日历日时，开始新的周期。
    如果周期跨度超过4个日历日，记录一次违规。

    Args:
        tasks_list (list): 按时间排序的、单个机组的所有任务。
        crew_base (str): 该机组的所属基地。
    """
    violation_counts = 0
    if not tasks_list or len(tasks_list) == 0:
        return 0

    all_cycles = []
    current_cycle = [tasks_list[0]]  # 从第一个任务开始
    
    # 从第二个任务开始遍历
    for i in range(1, len(tasks_list)):
        task_current = tasks_list[i]
        task_prev = tasks_list[i-1]
        
        # 计算与上一个任务的间隔天数
        rest_start_time = task_prev['endTime']
        rest_end_time = task_current['startTime']
        # 计算完整日历日休息天数
        full_rest_days = (rest_end_time.date() - rest_start_time.date()).days - 1
        
        if full_rest_days >= 2:
            # 休息天数大于等于2天，结束当前周期，开始新周期
            all_cycles.append(current_cycle)
            current_cycle = [task_current]
        else:
            # 休息天数不足2天，继续当前周期
            current_cycle.append(task_current)
    
    # 添加最后一个周期
    if current_cycle:
        all_cycles.append(current_cycle)
    
    # 检查每个周期的跨度是否超过4天
    for cycle in all_cycles:
        if len(cycle) > 0:
            cycle_start_date = cycle[0]['startTime'].date()
            cycle_end_date = cycle[-1]['endTime'].date()
            cycle_span_days = (cycle_end_date - cycle_start_date).days + 1
            
            if cycle_span_days > 4:
                violation_counts += 1
    
    return violation_counts
    
# --- 主评分函数 ---
def calculate_score(data_path, submission_path):
    """
    根据给定的 submission.csv 文件计算总分并按指定格式输出。
    """
    tasks_df, ground_duty_df, crew_df, valid_pairings, allowed_layover_stations, flights_df = load_data(data_path)
    if tasks_df is None: return

    try:
        submission_df = pd.read_csv(submission_path)
    except FileNotFoundError:
        print(f"错误：找不到提交文件 {submission_path}。")
        return

    # --- 1. 统一所有任务来源 ---
    # 处理提交的任务 (航班/置位)
    submitted_tasks = submission_df.merge(tasks_df, on='taskId', how='left')
    submitted_tasks['isDuty'] = 1 # 航班和置位都算值勤  

    # 将所有任务合并到一个DataFrame
    all_tasks_df = pd.concat([submitted_tasks, ground_duty_df], sort=False)
    all_tasks_df.fillna({'flightMinutes': 0}, inplace=True)

    # --- 初始化所有指标和违规计数器 ---
    scores = {
        "duty_day_avg_flight_time_score": 0,
        "uncovered_flights_penalty": 0,
        "new_layover_stations_penalty": 0,
        "out_of_base_layover_penalty": 0,
        "ddh_penalty": 0,
        "violations_penalty": 0
    }
    # <--- 逻辑修改: 使用字典详细记录各项违规 ---
    violation_counts = {
        '任务重叠': 0,
        '多机长执飞': 0,
        '置位位置': 0,           # 规则1
        '地点衔接规则': 0,       # 规则2
        '连接时间': 0,           # 规则3
        '飞行值勤日飞行任务数量限制': 0, # 规则4a
        '飞行值勤日值勤任务数量限制': 0, # 规则4b
        '飞行值勤日最大飞行时间限制': 0, # 规则5
        '飞行值勤日最大飞行值勤时间限制': 0, # 规则6
        '飞行值勤日最小休息时间限制': 0, # 规则7 (理论上为0)
        '值四休二要求': 0,       # 规则8
        '总飞行值勤时间限制': 0,     # 规则9
        '资质检查': 0            # 规则10
    }
    
    total_flight_minutes = 0
    total_duty_days = 0
    total_out_of_base_days = 0
    new_layover_stations = set()
    total_duty_calendar_days = 0

    # --- 全局检查 ---
    # 检查: 多机长执飞
    task_assignments = submitted_tasks[submitted_tasks['taskId'].str.startswith('Flt') & submitted_tasks['isDDH'] == 0]['taskId'].value_counts()
    overlap = task_assignments[task_assignments > 1]
    violation_counts['多机长执飞'] = len(task_assignments[task_assignments > 1])

    # --- 计算基础指标 ---
    all_flight_ids = set(flights_df['taskId'])
    submitted_flight_ids = set(submitted_tasks[submitted_tasks['isDDH'] == 0]['taskId'])
    uncovered_flights_count = len(all_flight_ids - submitted_flight_ids)
    ddh_count = submitted_tasks['isDDH'].sum()
    
    grouped_with_ground_duty = all_tasks_df.groupby('crewId')
    for crew_id, crew_schedule in grouped_with_ground_duty:
        # 检查: 初始地点衔接 (规则 2)
        crew_schedule = crew_schedule.sort_values(by='startTime').reset_index(drop=True)
        crew_info = crew_df.loc[crew_id]
        initial_station = crew_info['stayStation']
        if crew_schedule['depaAirport'].iloc[0] != initial_station:
            violation_counts['地点衔接规则'] += 1
    
    # --- 按机组进行详细检查 ---
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
        # 检查: 资质 (规则 10)
        for task in tasks_list:
            if (crew_id, task['taskId']) not in valid_pairings and task['taskId'].startswith('Flt'):
                violation_counts['资质检查'] += 1


        # 计算: 外站过夜 (情况②)
        if initial_station != crew_base:
            days = (tasks_list[0]['startTime'].date() - PLAN_START_DATE).days
            total_out_of_base_days += max(0, days)
        
        all_duty_days, violation_counts = split_duty_periods(tasks_list_all_with_rest, violation_counts)
        
        # 分析每个值勤日
        crew_total_duty_minutes = 0
        for duty_day_tasks in all_duty_days:
            duty_day_df = pd.DataFrame(duty_day_tasks)
            
            duty_start_time = duty_day_df['startTime'].iloc[0]
            duty_end_time = duty_day_df['endTime'].iloc[-1]
            duty_day_flight_minutes = duty_day_df[duty_day_df['taskId'].str.startswith('Flt')]['flightMinutes'].sum()
            total_flight_minutes += duty_day_flight_minutes
            total_duty_calendar_days += (duty_end_time.date() - duty_start_time.date()).days + 1
            
            # 检查: 置位位置 (规则 1)
            if len(duty_day_df) > 2:
                if duty_day_df.iloc[1:-1]['isDDH'].sum() > 0:
                     violation_counts['置位位置'] += duty_day_df.iloc[1:-1]['isDDH'].sum()

            # 检查: 连接时间 (规则 3)
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

            # 检查: 任务数限制 (规则 4)
            flight_tasks_count = duty_day_df[duty_day_df['taskId'].str.startswith('Flt')].shape[0]
            if flight_tasks_count > 4: violation_counts['飞行值勤日飞行任务数量限制'] += 1
            if len(duty_day_df) > 6: violation_counts['飞行值勤日值勤任务数量限制'] += 1
            
            # 检查: 最大飞行时间 (规则 5)
            if (duty_day_flight_minutes / 60) > 8: violation_counts['飞行值勤日最大飞行时间限制'] += 1

            # 检查: 最大值勤时间 (规则 6)
            flight_tasks_in_duty = duty_day_df[duty_day_df['taskId'].str.startswith('Flt')]
            if not flight_tasks_in_duty.empty:
                duration = flight_tasks_in_duty['endTime'].iloc[-1] - duty_day_df['startTime'].iloc[0]
                if duration > timedelta(hours=12): 
                    violation_counts['飞行值勤日最大飞行值勤时间限制'] += 1
                crew_total_duty_minutes += duration.total_seconds() / 60

        # 检查: 总值勤时间 (规则 9)
        if (crew_total_duty_minutes / 60) > 60: violation_counts['总飞行值勤时间限制'] += 1

        for j in range(len(all_duty_days) - 1):
            prev_duty_df = pd.DataFrame(all_duty_days[j])
            next_duty_df = pd.DataFrame(all_duty_days[j+1])
            prev_duty_end_time = prev_duty_df['endTime'].iloc[-1]
            next_duty_start_time = next_duty_df['startTime'].iloc[0]
            rest_location = prev_duty_df['arriAirport'].iloc[-1]
            
            # --- 外站过夜天数: 情况① 和 ③ (值勤日之间) ---
            # <--- 逻辑修改: 此处逻辑更新以更清晰地匹配规则 ---
            if rest_location != crew_base:
                days_diff = (next_duty_start_time.date() - prev_duty_end_time.date()).days
                if days_diff == 0:
                    # 情况③: 同日休息，计1天
                    total_out_of_base_days += 1
                else:
                    # 情况①: 跨零点休息，计跨零点天数
                    total_out_of_base_days += days_diff
            # <--- 逻辑修改结束 ---

            # 新增过夜站点检查
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

        # --- 外站过夜天数: 情况④ (最终停留) ---
        if final_station != crew_base:
            days = (PLAN_END_DATE - crew_schedule.iloc[-1]['endTime'].date()).days
            total_out_of_base_days += max(0, days)

    # --- 汇总分数 ---
    avg_flight_hours_per_duty_day = (total_flight_minutes / 60) / total_duty_calendar_days if total_duty_calendar_days > 0 else 0
    scores['duty_day_avg_flight_time_score'] = avg_flight_hours_per_duty_day * 1000
    scores['uncovered_flights_penalty'] = -5 * uncovered_flights_count
    scores['new_layover_stations_penalty'] = -10 * len(new_layover_stations)
    scores['out_of_base_layover_penalty'] = -0.5 * total_out_of_base_days
    scores['ddh_penalty'] = -0.5 * ddh_count
    total_violations = sum(violation_counts.values())
    scores['violations_penalty'] = -10 * total_violations
    total_score = sum(scores.values())

    # --- 按指定格式输出 ---
    output_str = (
        f"总得分：{total_score:.2f} "
        f"值勤日日均飞时：{avg_flight_hours_per_duty_day:.2f} "
        f"未覆盖航班数：{uncovered_flights_count} "
        f"外站过夜天数：{total_out_of_base_days} "
        f"新增过夜机场数：{len(new_layover_stations)} "
        f"置位次数：{ddh_count} "
        f"任务重叠：{violation_counts['任务重叠']} "
        f"多机长执飞：{violation_counts['多机长执飞']} "
        f"置位位置：{violation_counts['置位位置']} "
        f"地点衔接规则：{violation_counts['地点衔接规则']} "
        f"连接时间：{violation_counts['连接时间']} "
        f"飞行值勤日飞行任务数量限制：{violation_counts['飞行值勤日飞行任务数量限制']} "
        f"飞行值勤日值勤任务数量限制：{violation_counts['飞行值勤日值勤任务数量限制']} "
        f"飞行值勤日最大飞行时间限制：{violation_counts['飞行值勤日最大飞行时间限制']} "
        f"飞行值勤日最大飞行值勤时间限制：{violation_counts['飞行值勤日最大飞行值勤时间限制']} "
        f"飞行值勤日最小休息时间限制：{violation_counts['飞行值勤日最小休息时间限制']} "
        f"值四休二要求：{violation_counts['值四休二要求']} "
        f"总飞行值勤时间限制：{violation_counts['总飞行值勤时间限制']} "
        f"资质检查：{violation_counts['资质检查']}"
    )
    print(output_str)


# --- 程序入口 ---
if __name__ == '__main__':
    # 将'submission.csv'替换为你的提交文件名
    data_path = '/home/ubuntu/new-cts/data/0703/'
    submission_file = '/home/ubuntu/new-cts/result_0703.csv'
    calculate_score(data_path, submission_file)