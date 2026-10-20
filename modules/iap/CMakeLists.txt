if(CONFIG_IAP)
  set(TINYCRYPT ${ZEPHYR_MCUBOOT_MODULE_DIR}/ext/tinycrypt/lib)
  zephyr_library()
  zephyr_include_directories(include)
  zephyr_library_include_directories(${TINYCRYPT}/include)
  zephyr_library_sources(src/receiver.c src/sgz.c src/verify_rng.c
    ${TINYCRYPT}/source/aes_encrypt.c ${TINYCRYPT}/source/aes_decrypt.c
    ${TINYCRYPT}/source/sha256.c ${TINYCRYPT}/source/utils.c
    ${TINYCRYPT}/source/ecc.c ${TINYCRYPT}/source/ecc_dsa.c)
  if(CONFIG_IAP_CONFIGURED)
    get_filename_component(IAP_PUBLIC_KEY "${CONFIG_IAP_PUBLIC_KEY_FILE}" ABSOLUTE
      BASE_DIR "${APPLICATION_CONFIG_DIR}")
    execute_process(COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/scripts/public_key.py
      "${IAP_PUBLIC_KEY}" ${CMAKE_CURRENT_BINARY_DIR}/iap_keys.h "${CONFIG_IAP_AES_KEY_HEX}"
      COMMAND_ERROR_IS_FATAL ANY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      "${IAP_PUBLIC_KEY}" ${CMAKE_CURRENT_LIST_DIR}/scripts/public_key.py)
    zephyr_library_include_directories(${CMAKE_CURRENT_BINARY_DIR})
    zephyr_library_sources(src/config.c)
  endif()
endif()
