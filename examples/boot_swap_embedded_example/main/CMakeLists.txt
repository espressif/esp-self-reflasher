idf_build_get_property(project_dir PROJECT_DIR)
idf_component_register(SRCS "boot_swap_embedded.c"
                       INCLUDE_DIRS "."
                       LDFRAGMENTS ../boot_swap_embedded.lf)

target_add_binary_data(${COMPONENT_LIB}
                       "${CONFIG_EXAMPLE_TARGET_BINARY_DIR_PATH}/${CONFIG_EXAMPLE_TARGET_BINARY_NAME}"
                       BINARY)

set(EXAMPLE_TARGET_BIN_START "_binary_${CONFIG_EXAMPLE_TARGET_BINARY_NAME}_start")
set(EXAMPLE_TARGET_BIN_END "_binary_${CONFIG_EXAMPLE_TARGET_BINARY_NAME}_end")

string(REPLACE "." "_" EXAMPLE_TARGET_BIN_START "${EXAMPLE_TARGET_BIN_START}")
string(REPLACE "." "_" EXAMPLE_TARGET_BIN_END "${EXAMPLE_TARGET_BIN_END}")

add_definitions(-DEXAMPLE_TARGET_BIN_START=\"${EXAMPLE_TARGET_BIN_START}\")
add_definitions(-DEXAMPLE_TARGET_BIN_END=\"${EXAMPLE_TARGET_BIN_END}\")
