idf_component_register(SRCS "hc_main.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "certs/ssllabs.com"
                                "certs/ssllabs_root.com"
                                "certs/naver_root.crt"
                                "certs/servercert.pem"
                                "certs/prvtkey.pem"
                                "web/index.html")
