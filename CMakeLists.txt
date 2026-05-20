cmake_minimum_required(VERSION 3.10)

project(mavrelayclient C)

set(CMAKE_C_STANDARD 99)

# 1. Quelldateien definieren
set(SOURCES
    src/main.c
    src/serial.c
    src/udp.c
    src/option.c
    src/logging.c
    src/console.c
    src/param_cache.c
    cJSON/cJSON.c
)

# 2. Das Target erstellen (MUSS vor target_... Befehlen kommen)
add_executable(${PROJECT_NAME} ${SOURCES})

# 3. Include-Verzeichnisse setzen
target_include_directories(${PROJECT_NAME} PUBLIC 
    ${PROJECT_SOURCE_DIR}/include mavlink/include/mavlink/v2.0 ${PROJECT_SOURCE_DIR}/cJSON)


# 4. Plattformspezifische Optionen und Bibliotheken (Reihenfolge korrigiert)
if (CMAKE_HOST_SYSTEM_NAME STREQUAL "SunOS")
   if (CMAKE_HOST_SYSTEM STREQUAL "SunOS-5.11")
      set(PROJECT_LINK_LIBS -L/usr/lib -lssl -lsocket -lnsl -lgen -lpam -lbsm -lpthread -lcrypto /usr/local/lib/64/liblog4cpp.a)
      target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_NEWPAM)
   else()
      set(PROJECT_LINK_LIBS -L/usr/lib -lssl -lsocket -lnsl -lgen -lpam -lbsm -lpthread -lcrypto /usr/local/lib/liblog4cpp.a)
   endif()
   target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_LINK_LIBS})

elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
   # Hier stand es falsch – jetzt greift es korrekt auf das existierende Target zu
   target_compile_options(${PROJECT_NAME} PRIVATE -DUSE_NO_MDNS)
   # Behebt die "Undefined symbols" auf dem Mac
   target_link_libraries(${PROJECT_NAME} PRIVATE edit)

else()
   # Für Linux und andere OS
   target_compile_options(${PROJECT_NAME} PRIVATE -DUSE_MDNS)
   # Behebt die "Undefined symbols" unter Linux
   target_link_libraries(${PROJECT_NAME} PRIVATE readline)
endif()
