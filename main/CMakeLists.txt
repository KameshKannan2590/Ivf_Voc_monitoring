idf_component_register(
    SRCS
        "app_main.c"
        "display/display_driver.c"
        "display/display_power.c"   # Phase 6 — screen dim/wake/timeout state machine
        "touch/touch_driver.c"
        "lvgl_port/lvgl_port.c"
        "ui/ui.c"
        # "ui/nav_drawer.c"          # Phase 4B legacy — removed Phase 4.2.5 (superseded by navigation_drawer)
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
        "data/history_manager.c"    # Phase 5.3 — history backend (LVGL/UI independent)
        "data/calendar_util.c"      # Phase 5.8 — shared boot-ts -> calendar-date math (LVGL/UI independent)
        "data/config_manager.c"     # Phase 6 — settings persistence (LVGL/UI independent)
        # ── Phase 4.1 — Shared UI components ──────────────────────────────────
        "ui/assets/assets.c"
        "ui/assets/menu_burger_horizontal_bold.c"
        "ui/assets/temperature.c"
        "ui/assets/humidity.c"
        "ui/assets/sd_Card.c"
        "ui/assets/dashboard_icon.c"
        "ui/assets/chart_icon.c"
        "ui/assets/data_log_icon.c"
        "ui/assets/setting_icon.c"
        "ui/assets/shield_icon.c"
        "ui/assets/wifi_icon.c"
        # ── Chart screen icons — real bitmaps (Phase 5.4.1) ────────────────────
        "ui/assets/calendar_icon.c"
        "ui/assets/date_range_icon.c"
        "ui/assets/chart_average_icon.c"
        "ui/assets/chart_maximum_icon.c"
        "ui/assets/chart_minimum_icon.c"
        # ── Logs screen icon — real bitmap (Phase 5.8) ──────────────────────────
        "ui/assets/datalog_icon.c"
        "ui/components/status_badge/status_badge.c"
        "ui/components/icon_button/icon_button.c"
        "ui/components/card/card.c"
        "ui/components/circular_gauge/circular_gauge.c"
        "ui/components/header/header.c"
        "ui/components/navigation_drawer/navigation_drawer.c"
        "ui/components/voc_gauge/voc_gauge.c"

    INCLUDE_DIRS
        "."
        "display"
        "touch"
        "lvgl_port"
        "ui"
        "ui/assets"
        "ui/components/status_badge"
        "ui/components/icon_button"
        "ui/components/card"
        "ui/components/circular_gauge"
        "ui/components/header"
        "ui/components/navigation_drawer"
        "ui/components/voc_gauge"
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
