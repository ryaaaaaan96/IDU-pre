set(TINYCRYPT ${ZEPHYR_MCUBOOT_MODULE_DIR}/ext/tinycrypt/lib)
execute_process(COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../../tools/iap_public_key.py
  ${CONFIG_MCUBOOT_SIGNATURE_KEY_FILE} ${CMAKE_CURRENT_BINARY_DIR}/iap_public_key.h
  COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CONFIG_MCUBOOT_SIGNATURE_KEY_FILE})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../../tools/iap_public_key.py)
target_include_directories(app PRIVATE ${TINYCRYPT}/include ${CMAKE_CURRENT_BINARY_DIR})
target_sources(app PRIVATE receiver.c sgz.c verify_rng.c
  ${TINYCRYPT}/source/aes_encrypt.c ${TINYCRYPT}/source/aes_decrypt.c
  ${TINYCRYPT}/source/sha256.c ${TINYCRYPT}/source/utils.c
  ${TINYCRYPT}/source/ecc.c ${TINYCRYPT}/source/ecc_dsa.c)
