idf_component_register(
    SRCS "main.c"
         "game.c"
         "day_cycle.c"
         "dust.c"
         "player.c"
         "obstacles.c"
         "scenery.c"
         "hud.c"
         "render.c"
         "input.c"
         "sfx.c"
         "sprites.c"
         "fap_screenshot.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_lcd esp_timer nvs_flash esp_driver_usb_serial_jtag
)
