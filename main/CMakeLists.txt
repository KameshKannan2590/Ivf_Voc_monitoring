idf_component_register(
    SRCS
        "app_main.c"
        "display/display_driver.c"
        "touch/touch_driver.c"
        "lvgl_port/lvgl_port.c"
        "ui/ui.c"
        "ui/screens/screen_splash.c"
        "ui/screens/screen_dashboard.c"
        "ui/screens/screen_chart.c"
        "ui/screens/screen_logs.c"
        "ui/screens/screen_settings.c"
        "sensors/sensor_manager.c"
        # ── Sensor backend (swap one line for Phase 7 real hardware) ──────────
        "sensors/sensor_backend_sim.c"      # simulation: comment out for real HW
        # "sensors/sensor_backend_hw.c"     # real HW:   uncomment for Phase 7
        "data/alarm_manager.c"

    INCLUDE_DIRS
        "."
        "display"
        "touch"
        "lvgl_port"
        "ui"
        "sensors"
        "data"

    REQUIRES
        nvs_flash
        esp_lcd
        esp_timer
        driver
        freertos
        log
)

# Tell LVGL where to find lv_conf.h
# LV_CONF_INCLUDE_SIMPLE means it looks for lv_conf.h in the component
# include path; we have it at main/lvgl_port/lv_conf.h which is in INCLUDE_DIRS
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    LV_CONF_INCLUDE_SIMPLE=1
)
