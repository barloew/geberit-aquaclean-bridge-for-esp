idf_component_register(
    SRCS "main.c" "arendi_common.c" "aquaclean_bridge_id.c" "arendi_client.c" "arendi_server.c" "alba_cmd.c" "web_portal.c"
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash mbedtls esp_hw_support esp_wifi esp_netif esp_event esp_http_server
)
