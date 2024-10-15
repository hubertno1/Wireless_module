# Stopwatch工程结构

```
├── CMakeLists.txt
├── board
│   ├── board.c
│   └── board.h
├── cableio
│   └── uart
│       ├── uart_manager.c
│       └── uart_manager.h
├── main.c
├── protos
│   ├── message.pb.c
│   ├── message.pb.h
│   └── message.proto
├── third_lib
├── utils
└── wireless
    ├── bluetooth
    └── wifi_station
        ├── service
        │   ├── get_weather
        │   │   ├── get_weather.c
        │   │   └── get_weather.h
        │   └── sync_time
        │       ├── sync_time.c
        │       └── sync_time.h
        ├── wifi_station.c
        └── wifi_station.h
```
