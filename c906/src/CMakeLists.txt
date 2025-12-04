add_executable(c906.elf
    start.S
    main.c
    uart.c
    resource_table.c
)

# Ensure the linker script is tracked as a dependency for this target.
set_target_properties(c906.elf PROPERTIES LINK_DEPENDS "${C906_LINKER_SCRIPT}")
target_link_libraries(c906.elf PRIVATE libstring)
