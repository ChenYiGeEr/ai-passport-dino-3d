idf_component_register(
    SRCS "main.c"
         "game.c"
         "player.c"
         "obstacles.c"
         "scenery.c"
         "hud.c"
         "render.c"
         "input.c"
         "sfx.c"
         "sprites.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_lcd esp_timer nvs_flash
)
