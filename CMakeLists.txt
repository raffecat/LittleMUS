add_library(LittleMUS STATIC
    musplayer.c
    musdriver.c
)
target_include_directories(LittleMUS PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# LittleMUS needs the Nuked-OPL3 header.
target_link_libraries(LittleMUS PRIVATE NukedOPL3)
