idf_component_register(
    SRCS "src/crypto.c"
         "src/wireguard.c"
         "src/wireguardif.c"
         "src/wireguard-platform.c"
         "src/crypto/refc/blake2s.c"
         "src/crypto/refc/chacha20.c"
         "src/crypto/refc/chacha20poly1305.c"
         "src/crypto/refc/poly1305-donna.c"
         "src/crypto/refc/x25519.c"
         "src/esp_wireguard.c"
         "src/nacl/crypto_scalarmult/curve25519/ref/smult.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_netif lwip mbedtls esp_hw_support esp_timer)

if(${IDF_VERSION_MAJOR} GREATER_EQUAL 5)
    set_source_files_properties(src/crypto/refc/x25519.c
        PROPERTIES COMPILE_FLAGS
        -Wno-error=stringop-overread)
endif()

# Sofia: third-party C written for older compilers; GCC 15 turns its string
# initialisers and a few other habits into errors.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
