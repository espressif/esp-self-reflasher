idf_build_get_property(project_dir PROJECT_DIR)
idf_component_register(SRCS "ota_reflash.c"
                       INCLUDE_DIRS "."
                       # Embed the server root certificate into the final binary
                       # EMBED_TXTFILES ${project_dir}/test_certs/server_cert.pem
                       )
