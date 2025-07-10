import pandas as pd
import os
from datetime import datetime

def main():
    # 数据路径
    data_path = '/home/ubuntu/new-cts/data/0703/'
    output_dir = '/home/ubuntu/new-cts/'
    
    # 确保输出目录存在
    os.makedirs(output_dir, exist_ok=True)
    
    # 加载数据
    crew_df = pd.read_csv(data_path + 'crew.csv')
    ground_duty_df = pd.read_csv(data_path + 'groundDuty.csv')
    crew_leg_match_df = pd.read_csv(data_path + 'crewLegMatch.csv')
    bus_df = pd.read_csv(data_path + 'busInfo.csv')
    
    # 时间格式转换
    bus_df['td'] = pd.to_datetime(bus_df['td'])
    bus_df['ta'] = pd.to_datetime(bus_df['ta'])
    ground_duty_df['startTime'] = pd.to_datetime(ground_duty_df['startTime'])
    ground_duty_df['endTime'] = pd.to_datetime(ground_duty_df['endTime'])
    
    # 获取有资质的机组ID列表
    qualified_crews = set(crew_leg_match_df['crewId'].unique())
    
    # 获取有地面任务的机组ID列表
    crews_with_ground_duties = set(ground_duty_df['crewId'].unique())
    
    # 找出有地面任务但没有飞行资质的机组
    no_qua_crews = crews_with_ground_duties - qualified_crews
    
    print(f"找到{len(no_qua_crews)}个有地面任务但没有飞行资质的机组")
    
    # 结果数据
    result_data = []
    
    # 为每个无资质机组分配巴士置位任务
    for crew_id in no_qua_crews:
        # 获取该机组的地面任务
        crew_ground_duties = ground_duty_df[ground_duty_df['crewId'] == crew_id].sort_values('startTime')
        
        # 获取该机组的基地和当前位置
        crew_info = crew_df[crew_df['crewId'] == crew_id].iloc[0]
        base = crew_info['base']
        stay_station = crew_info['stayStation']
        
        # 如果机组不在基地，为其分配从当前位置到基地的巴士任务
        if stay_station != base:
            # 查找合适的巴士任务（从当前位置到基地）
            suitable_buses = bus_df[(bus_df['depaAirport'] == stay_station) & 
                                   (bus_df['arriAirport'] == base)]
            
            if not suitable_buses.empty:
                # 选择出发时间最早的巴士
                best_bus = suitable_buses.sort_values('td').iloc[0]
                
                # 添加到结果
                result_data.append({
                    'crewId': crew_id,
                    'taskId': best_bus['id'],
                    'isDDH': 1  # 巴士任务标记为置位
                })
                print(f"为机组 {crew_id} 分配了从 {stay_station} 到 {base} 的巴士任务: {best_bus['id']}")
            else:
                print(f"警告: 未找到从 {stay_station} 到 {base} 的巴士任务，机组 {crew_id} 无法返回基地")
        
    # 创建结果DataFrame
    result_df = pd.DataFrame(result_data)
    
    # 保存结果
    result_df.to_csv(output_dir + 'rosterResult.csv', index=False)
    print(f"结果已保存到 {output_dir}rosterResult.csv")

if __name__ == "__main__":
    main()
