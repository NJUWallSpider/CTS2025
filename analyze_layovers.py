import pandas as pd

flights = pd.read_csv('/home/dbxp/new-cts/data/0623/flight.csv')
buses = pd.read_csv('/home/dbxp/new-cts/data/0623/busInfo.csv')

# 统计 flights 中 depaAirport 和 arriAirport 中共同的不重复值
flights_depa_airports = flights['depaAirport'].unique()
flights_arri_airports = flights['arriAirport'].unique()
buses_depa_airports = buses['depaAirport'].unique()
buses_arri_airports = buses['arriAirport'].unique()

common_airports = set(flights_depa_airports) | set(flights_arri_airports) | set(buses_depa_airports) | set(buses_arri_airports)

print(common_airports)
print(len(common_airports))
print(len(flights_arri_airports))






