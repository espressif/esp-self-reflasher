idf_build_get_property(project_dir PROJECT_DIR)
idf_component_register(SRCS "boot_swap_download.c"
                       INCLUDE_DIRS "."
                       # Embed the server root certificate into the final binary
                       # EMBED_TXTFILES ${project_dir}/test_certs/server_cert.pem
                       LDFRAGMENTS ../boot_swap_download.lf)
