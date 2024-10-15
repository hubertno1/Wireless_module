set(srcs "main.c")
set(include_dirs ".")

file(GLOB CABLEIO_SRCS "cableio/*.c"
                       "cableio/uart/*.c")
list(APPEND srcs ${CABLEIO_SRCS})
list(APPEND include_dirs "cableio"
                         "cableio/uart")

file(GLOB BOARD_SRCS "board/*.c")
list(APPEND srcs ${BOARD_SRCS})
list(APPEND include_dirs "board")

file(GLOB WIFI_SOURCES "wireless/wifi_station/*.c"
                       "wireless/wifi_station/service/*.c"
                       "wireless/wifi_station/service/get_weather/*.c"
                       "wireless/wifi_station/service/sync_time/*.c"
                       "wireless/wifi_station/service/update_firmware/*.c")
list(APPEND srcs ${WIFI_SOURCES})
list(APPEND include_dirs "wireless/wifi_station"
                         "wireless/wifi_station/service"
                         "wireless/wifi_station/service/get_weather"
                         "wireless/wifi_station/service/sync_time"
                         "wireless/wifi_station/service/update_firmware")

# 添加 nanopb 的源文件和包含目录
list(APPEND srcs
    "third_lib/nanopb/pb_common.c"
    "third_lib/nanopb/pb_decode.c"
    "third_lib/nanopb/pb_encode.c"
)

list(APPEND include_dirs "third_lib/nanopb")

# 添加生成的 protobuf 文件
file(GLOB PROTO_SRCS "protos/*.c")
list(APPEND srcs ${PROTO_SRCS})
list(APPEND include_dirs "protos")

# 添加 crc 文件夹中的源文件和包含目录
file(GLOB CRC_SRCS "third_lib/crc/*.c")
list(APPEND srcs ${CRC_SRCS})
list(APPEND include_dirs "third_lib/crc")

# 添加 ASAN 库的源文件和包含目录
file(GLOB ASAN_SRCS "third_lib/asan/*.c")
list(APPEND srcs ${ASAN_SRCS})
list(APPEND include_dirs "third_lib/asan")

# 这里移除了引号
idf_component_register(SRCS ${srcs} INCLUDE_DIRS ${include_dirs})

# 设置特定源文件的编译选项
set_source_files_properties("main.c" 
    PROPERTIES COMPILE_FLAGS "-fsanitize=kernel-address")

