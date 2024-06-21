idf_component_register(SRCS "raw_ota_main.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "app_fw/hello_world_pv.bin"
                    EMBED_FILES "app_fw/hello_world_val.bin"
                    EMBED_FILES "app_fw/hello_world_inv.bin")
