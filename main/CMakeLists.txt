set(driver_requires driver)

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.3")
    set(driver_requires esp_driver_gpio esp_driver_spi)
endif()

idf_component_register(
    SRCS
        "main.cpp"
        "app_eth.cpp"
        "app_led.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_eth
        esp_event
        esp_netif
        ${driver_requires}
        nvs_flash
)
