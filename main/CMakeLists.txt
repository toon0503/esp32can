idf_component_register(SRCS "ws_echo_server.c"
                    PRIV_REQUIRES esp_http_server esp_wifi nvs_flash esp_eth
                    INCLUDE_DIRS ".")

# 添加index.html作为嵌入式文件
target_add_binary_data(${COMPONENT_LIB} "index.html" BINARY)