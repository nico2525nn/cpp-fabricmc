cmake_minimum_required(VERSION 3.20)
project(cppfabricmc VERSION 1.21.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(CPPFM_WARNINGS_AS_ERRORS "Treat supported C++ compiler warnings as errors" ON)
set(CPPFM_BUILD_TESTS_DEFAULT ON)
if(WIN32)
    # The historical live-server harnesses use fork()/exec()/socketpair().
    # Keep the production target portable without claiming those harnesses
    # are Windows-compatible before they are independently ported.
    set(CPPFM_BUILD_TESTS_DEFAULT OFF)
endif()
option(CPPFM_BUILD_TESTS "Build the native and live-server test targets"
       ${CPPFM_BUILD_TESTS_DEFAULT})
if(MSVC)
    add_compile_options(/W4 /permissive- /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter -pipe)
    if(CPPFM_WARNINGS_AS_ERRORS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-Werror)
    endif()
endif()

# Java shadow ABI and embedded HotSpot boundary. JNI remains optional at
# configure time: the executable can still run natively on a host without a
# JDK, while a JDK enables the default-on Fabric-compatible Java path.
find_package(JNI QUIET)
find_program(CPPFM_JAVAC NAMES javac)
find_program(CPPFM_JAVA NAMES java)
# FindJava knows about platform-specific JDK layouts (including macOS
# .jdk/Contents/Home bundles) better than a shell PATH lookup alone.  Keep
# the direct find_program results when present, but use the module as a
# fallback so a normal JDK installation enables the default JVM path on all
# supported host platforms.
find_package(Java 17 COMPONENTS Development QUIET)
if(NOT CPPFM_JAVA AND Java_JAVA_EXECUTABLE)
    set(CPPFM_JAVA "${Java_JAVA_EXECUTABLE}")
endif()
if(NOT CPPFM_JAVAC AND Java_JAVAC_EXECUTABLE)
    set(CPPFM_JAVAC "${Java_JAVAC_EXECUTABLE}")
endif()

# FindJNI historically depends on JAVA_HOME or a platform-specific registry
# hint.  If the launcher is known but FindJNI did not populate its include
# paths, derive the JDK home from the resolved launcher and fill the minimal
# JNI variables needed by the dynamically-loaded HotSpot bridge.  The bridge
# still locates and loads libjvm/jvm.dll at runtime, so this is intentionally a
# compile-time discovery fallback rather than a link-time JVM dependency.
if(NOT JNI_FOUND AND CPPFM_JAVA)
    get_filename_component(CPPFM_JAVA_REAL "${CPPFM_JAVA}" REALPATH)
    get_filename_component(CPPFM_JAVA_BIN "${CPPFM_JAVA_REAL}" DIRECTORY)
    get_filename_component(CPPFM_JAVA_HOME "${CPPFM_JAVA_BIN}" DIRECTORY)
    if(WIN32)
        set(CPPFM_JNI_PLATFORM_INCLUDE "${CPPFM_JAVA_HOME}/include/win32")
        set(CPPFM_JVM_LIBRARY_CANDIDATES
            "${CPPFM_JAVA_HOME}/bin/server/jvm.dll"
            "${CPPFM_JAVA_HOME}/jre/bin/server/jvm.dll"
            "${CPPFM_JAVA_HOME}/jre/bin/client/jvm.dll")
    elseif(APPLE)
        set(CPPFM_JNI_PLATFORM_INCLUDE "${CPPFM_JAVA_HOME}/include/darwin")
        set(CPPFM_JVM_LIBRARY_CANDIDATES
            "${CPPFM_JAVA_HOME}/lib/server/libjvm.dylib"
            "${CPPFM_JAVA_HOME}/jre/lib/server/libjvm.dylib")
    else()
        set(CPPFM_JNI_PLATFORM_INCLUDE "${CPPFM_JAVA_HOME}/include/linux")
        set(CPPFM_JVM_LIBRARY_CANDIDATES
            "${CPPFM_JAVA_HOME}/lib/server/libjvm.so"
            "${CPPFM_JAVA_HOME}/jre/lib/amd64/server/libjvm.so"
            "${CPPFM_JAVA_HOME}/jre/lib/server/libjvm.so")
    endif()
    set(CPPFM_JNI_INCLUDE "${CPPFM_JAVA_HOME}/include")
    set(CPPFM_JVM_LIBRARY_FALLBACK "")
    foreach(CPPFM_JVM_CANDIDATE IN LISTS CPPFM_JVM_LIBRARY_CANDIDATES)
        if(IS_REGULAR_FILE "${CPPFM_JVM_CANDIDATE}")
            set(CPPFM_JVM_LIBRARY_FALLBACK "${CPPFM_JVM_CANDIDATE}")
            break()
        endif()
    endforeach()
    if(IS_DIRECTORY "${CPPFM_JNI_INCLUDE}" AND
       IS_DIRECTORY "${CPPFM_JNI_PLATFORM_INCLUDE}" AND
       CPPFM_JVM_LIBRARY_FALLBACK)
        set(JNI_FOUND TRUE)
        set(JNI_INCLUDE_DIRS "${CPPFM_JNI_INCLUDE};${CPPFM_JNI_PLATFORM_INCLUDE}")
        set(JNI_JVM_LIBRARY "${CPPFM_JVM_LIBRARY_FALLBACK}")
        message(STATUS "JNI fallback discovered from ${CPPFM_JAVA_HOME}")
    endif()
endif()

unset(CPPFM_JNI_PLATFORM_INCLUDE)
unset(CPPFM_JNI_INCLUDE)
unset(CPPFM_JVM_LIBRARY_CANDIDATES)
unset(CPPFM_JVM_LIBRARY_FALLBACK)
find_package(Python3 COMPONENTS Interpreter QUIET)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)

if(WIN32)
    set(CPPFM_JAVA_CLASSPATH_SEPARATOR ";")
else()
    set(CPPFM_JAVA_CLASSPATH_SEPARATOR ":")
endif()

if(WIN32)
    set(CPPFM_PLATFORM_LIBS ws2_32)
else()
    set(CPPFM_PLATFORM_LIBS ${CMAKE_DL_LIBS})
endif()

set(CPPFM_EMBEDDED_DIR "${CMAKE_CURRENT_BINARY_DIR}/embedded")
set(CPPFM_EMBEDDED_HEADER "${CPPFM_EMBEDDED_DIR}/EmbeddedResources.hpp")

set(CPPFM_JVM_SHADOW_SPEC "${CMAKE_CURRENT_SOURCE_DIR}/jvm/shadow_api.json")
set(CPPFM_JVM_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/jvm/compatibility-manifest.json")
file(GLOB_RECURSE CPPFM_JAVA_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/jvm/java/*.java")
if(Python3_Interpreter_FOUND)
    add_custom_command(
        OUTPUT "${CPPFM_JVM_MANIFEST}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_shadow.py"
                --input "${CPPFM_JVM_SHADOW_SPEC}" --output "${CPPFM_JVM_MANIFEST}"
        DEPENDS "${CPPFM_JVM_SHADOW_SPEC}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_shadow.py"
                ${CPPFM_JAVA_SOURCES}
        VERBATIM)
    add_custom_target(cppfm_jvm_manifest ALL DEPENDS "${CPPFM_JVM_MANIFEST}")
endif()

set(CPPFM_JVM_CLASSES_DIR "${CMAKE_CURRENT_BINARY_DIR}/jvm/classes")
file(TO_CMAKE_PATH "${CPPFM_JVM_CLASSES_DIR}" CPPFM_DEFAULT_JVM_CLASSES_PATH)
if(CPPFM_JAVAC AND CPPFM_JAVA_SOURCES AND Python3_Interpreter_FOUND)
    set(CPPFM_JVM_COMPILE_STUB_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/jvm/compile-stubs/io/netty/buffer/ByteBuf.java"
        "${CMAKE_CURRENT_SOURCE_DIR}/jvm/compile-stubs/io/netty/buffer/ByteBufAllocator.java"
        "${CMAKE_CURRENT_SOURCE_DIR}/jvm/compile-stubs/io/netty/buffer/UnpooledHeapByteBuf.java"
        "${CMAKE_CURRENT_SOURCE_DIR}/jvm/compile-stubs/io/netty/buffer/UnpooledByteBufAllocator.java"
        "${CMAKE_CURRENT_SOURCE_DIR}/jvm/compile-stubs/com/mojang/serialization/MapCodec.java"
        "${CMAKE_CURRENT_SOURCE_DIR}/jvm/compile-stubs/com/mojang/serialization/MapEncoder.java"
        "${CMAKE_CURRENT_SOURCE_DIR}/jvm/compile-stubs/com/mojang/serialization/MapDecoder.java"
        "${CMAKE_CURRENT_SOURCE_DIR}/jvm/compile-stubs/com/mojang/serialization/codecs/RecordCodecBuilder.java"
        "${CMAKE_CURRENT_SOURCE_DIR}/jvm/compile-stubs/org/slf4j/Logger.java"
        "${CMAKE_CURRENT_SOURCE_DIR}/jvm/compile-stubs/org/slf4j/LoggerFactory.java")
    set(CPPFM_JVM_COMPILE_STUB_DIR "${CMAKE_CURRENT_BINARY_DIR}/jvm/compile-stubs")
    if(WIN32)
        set(CPPFM_JVM_COMPILE_CLASSPATH_SEPARATOR ";")
    else()
        set(CPPFM_JVM_COMPILE_CLASSPATH_SEPARATOR ":")
    endif()
    set(CPPFM_JVM_COMPILE_CLASSPATH
        "${CPPFM_JVM_CLASSES_DIR}${CPPFM_JVM_COMPILE_CLASSPATH_SEPARATOR}${CPPFM_JVM_COMPILE_STUB_DIR}")
    set(CPPFM_JVM_STAMP "${CPPFM_JVM_CLASSES_DIR}/.stamp")
    add_custom_command(
        OUTPUT "${CPPFM_JVM_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${CPPFM_JVM_CLASSES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${CPPFM_JVM_COMPILE_STUB_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CPPFM_JVM_CLASSES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CPPFM_JVM_COMPILE_STUB_DIR}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_with_timeout.py"
                --timeout 60 -- "${CPPFM_JAVAC}" --release 17 -encoding UTF-8 -g
                -d "${CPPFM_JVM_COMPILE_STUB_DIR}" ${CPPFM_JVM_COMPILE_STUB_SOURCES}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_with_timeout.py"
                --timeout 180 -- "${CPPFM_JAVAC}" --release 17 -encoding UTF-8 -g
                -cp "${CPPFM_JVM_COMPILE_STUB_DIR}"
                -d "${CPPFM_JVM_CLASSES_DIR}" ${CPPFM_JAVA_SOURCES}
        COMMAND ${CMAKE_COMMAND} -E touch "${CPPFM_JVM_STAMP}"
        DEPENDS ${CPPFM_JVM_COMPILE_STUB_SOURCES} ${CPPFM_JAVA_SOURCES}
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_with_timeout.py"
        VERBATIM)
    add_custom_target(cppfm_jvm_classes ALL DEPENDS "${CPPFM_JVM_STAMP}")

    set(CPPFM_JVM_FIXTURE_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_fixture/src/cppfm/fixture/FixtureMod.java"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_fixture/src/cppfm/fixture/mixin/ServerMixin.java")
    set(CPPFM_JVM_FIXTURE_METADATA
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_fixture/fabric.mod.json")
    set(CPPFM_JVM_FIXTURE_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/jvm/fixture-mods/cppfm_fixture")
    set(CPPFM_JVM_FIXTURE_STAMP "${CPPFM_JVM_FIXTURE_DIR}/.stamp")
    add_custom_command(
        OUTPUT "${CPPFM_JVM_FIXTURE_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${CPPFM_JVM_FIXTURE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CPPFM_JVM_FIXTURE_DIR}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_with_timeout.py"
                --timeout 90 -- "${CPPFM_JAVAC}" --release 17 -encoding UTF-8 -g
                -cp "${CPPFM_JVM_COMPILE_CLASSPATH}" -d "${CPPFM_JVM_FIXTURE_DIR}"
                ${CPPFM_JVM_FIXTURE_SOURCE}
        COMMAND ${CMAKE_COMMAND} -E copy "${CPPFM_JVM_FIXTURE_METADATA}"
                "${CPPFM_JVM_FIXTURE_DIR}/fabric.mod.json"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_fixture/cppfm_fixture.mixins.json"
                "${CPPFM_JVM_FIXTURE_DIR}/cppfm_fixture.mixins.json"
        COMMAND ${CMAKE_COMMAND} -E touch "${CPPFM_JVM_FIXTURE_STAMP}"
        # Depend on the classes stamp as a file as well as on the target.  The
        # target dependency only orders the build; the file dependency makes a
        # changed annotation/API class invalidate the fixture bytecode instead
        # of reusing a class compiled against an older shadow ABI.
        DEPENDS "${CPPFM_JVM_STAMP}" "${CPPFM_JVM_FIXTURE_SOURCE}"
                "${CPPFM_JVM_FIXTURE_METADATA}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_fixture/cppfm_fixture.mixins.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_with_timeout.py"
        VERBATIM)
    add_custom_target(cppfm_jvm_fixture DEPENDS "${CPPFM_JVM_FIXTURE_STAMP}")

    # plan51 §12/26: compile and execute the structural transformer contract
    # in the same Java class tree used by the embedded runtime.  Keeping this
    # as a build target makes defineClass/verifier coverage reproducible from
    # a clean checkout instead of relying on an ad-hoc temporary javac run.
    file(GLOB CPPFM_JVM_TRANSFORMER_SOURCES CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_transformer/*.java")
    if(CPPFM_JAVA AND CPPFM_JVM_TRANSFORMER_SOURCES)
        set(CPPFM_JVM_TRANSFORMER_DIR
            "${CMAKE_CURRENT_BINARY_DIR}/jvm/transformer-test")
        set(CPPFM_JVM_TRANSFORMER_STAMP
            "${CPPFM_JVM_TRANSFORMER_DIR}/.stamp")
        add_custom_command(
            OUTPUT "${CPPFM_JVM_TRANSFORMER_STAMP}"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${CPPFM_JVM_TRANSFORMER_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CPPFM_JVM_TRANSFORMER_DIR}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_with_timeout.py"
                    --timeout 90 -- "${CPPFM_JAVAC}" --release 17 -encoding UTF-8 -g
                    -cp "${CPPFM_JVM_COMPILE_CLASSPATH}"
                    -d "${CPPFM_JVM_TRANSFORMER_DIR}"
                    ${CPPFM_JVM_TRANSFORMER_SOURCES}
            COMMAND ${CMAKE_COMMAND} -E touch "${CPPFM_JVM_TRANSFORMER_STAMP}"
            DEPENDS "${CPPFM_JVM_STAMP}" ${CPPFM_JVM_TRANSFORMER_SOURCES}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_with_timeout.py"
            VERBATIM)
        add_custom_target(cppfm_jvm_transformer ALL
                          DEPENDS "${CPPFM_JVM_TRANSFORMER_STAMP}")
    endif()

    # plan51 §26: the Access Widener fixture is a focused byte-and-execution
    # gate.  Keep its source set independent of the optional intermediary
    # loader so a namespace mismatch cannot be hidden by that integration.
    file(GLOB_RECURSE CPPFM_JVM_ACCESS_WIDENER_CORE_SOURCES CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/jvm/java/cppfm/transform/*.java"
         "${CMAKE_CURRENT_SOURCE_DIR}/jvm/java/org/spongepowered/asm/mixin/*.java")
    list(APPEND CPPFM_JVM_ACCESS_WIDENER_CORE_SOURCES
         "${CMAKE_CURRENT_SOURCE_DIR}/jvm/java/cppfm/bridge/NativeBridge.java"
         "${CMAKE_CURRENT_SOURCE_DIR}/jvm/java/net/fabricmc/api/DedicatedServerModInitializer.java"
         "${CMAKE_CURRENT_SOURCE_DIR}/jvm/java/com/llamalad7/mixinextras/injector/wrapoperation/Operation.java")
    set(CPPFM_JVM_ACCESS_WIDENER_FIXTURE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_fixture/corpus/08-access-widener/src/cppfm/corpus/fixture08/AccessWidener.java"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_fixture/corpus/08-access-widener/src/cppfm/corpus/fixture08/AccessTarget.java"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_fixture/corpus/08-access-widener/AccessWidenerContractTest.java")
    set(CPPFM_JVM_ACCESS_WIDENER_RESOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_fixture/corpus/08-access-widener/fixture08.accesswidener")
    set(CPPFM_JVM_ACCESS_WIDENER_METADATA
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_fixture/corpus/08-access-widener/fabric.mod.json")
    if(CPPFM_JAVA AND CPPFM_JVM_ACCESS_WIDENER_CORE_SOURCES)
        set(CPPFM_JVM_ACCESS_WIDENER_DIR
            "${CMAKE_CURRENT_BINARY_DIR}/jvm/access-widener-test")
        set(CPPFM_JVM_ACCESS_WIDENER_STAMP
            "${CPPFM_JVM_ACCESS_WIDENER_DIR}/.stamp")
        add_custom_command(
            OUTPUT "${CPPFM_JVM_ACCESS_WIDENER_STAMP}"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${CPPFM_JVM_ACCESS_WIDENER_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CPPFM_JVM_ACCESS_WIDENER_DIR}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_with_timeout.py"
                    --timeout 90 -- "${CPPFM_JAVAC}" --release 17 -encoding UTF-8 -g
                    -d "${CPPFM_JVM_ACCESS_WIDENER_DIR}"
                    ${CPPFM_JVM_ACCESS_WIDENER_CORE_SOURCES}
                    ${CPPFM_JVM_ACCESS_WIDENER_FIXTURE_SOURCES}
            COMMAND ${CMAKE_COMMAND} -E copy "${CPPFM_JVM_ACCESS_WIDENER_RESOURCE}"
                    "${CPPFM_JVM_ACCESS_WIDENER_DIR}/fixture08.accesswidener"
            COMMAND ${CMAKE_COMMAND} -E copy "${CPPFM_JVM_ACCESS_WIDENER_METADATA}"
                    "${CPPFM_JVM_ACCESS_WIDENER_DIR}/fabric.mod.json"
            COMMAND ${CMAKE_COMMAND} -E touch "${CPPFM_JVM_ACCESS_WIDENER_STAMP}"
            DEPENDS ${CPPFM_JVM_ACCESS_WIDENER_CORE_SOURCES}
                    ${CPPFM_JVM_ACCESS_WIDENER_FIXTURE_SOURCES}
                    "${CPPFM_JVM_ACCESS_WIDENER_RESOURCE}"
                    "${CPPFM_JVM_ACCESS_WIDENER_METADATA}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_with_timeout.py"
            VERBATIM)
        add_custom_target(cppfm_jvm_access_widener ALL
                          DEPENDS "${CPPFM_JVM_ACCESS_WIDENER_STAMP}")
    endif()

    # plan51: executable Fabric API/event/command contract fixture.  Keep it
    # separate from the historical 25-mod corpus so the corpus count and its
    # reports remain stable while the newly implemented surface gets a direct
    # JVM-side execution gate.
    file(GLOB_RECURSE CPPFM_JVM_API_SOURCES CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_api/*.java")
    if(CPPFM_JAVA AND CPPFM_JVM_API_SOURCES)
        set(CPPFM_JVM_API_DIR "${CMAKE_CURRENT_BINARY_DIR}/jvm/api-test")
        set(CPPFM_JVM_API_STAMP "${CPPFM_JVM_API_DIR}/.stamp")
        add_custom_command(
            OUTPUT "${CPPFM_JVM_API_STAMP}"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${CPPFM_JVM_API_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CPPFM_JVM_API_DIR}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_with_timeout.py"
                    --timeout 90 -- "${CPPFM_JAVAC}" --release 17 -encoding UTF-8 -g
                    -cp "${CPPFM_JVM_COMPILE_CLASSPATH}" -d "${CPPFM_JVM_API_DIR}"
                    ${CPPFM_JVM_API_SOURCES}
            COMMAND ${CMAKE_COMMAND} -E touch "${CPPFM_JVM_API_STAMP}"
            DEPENDS "${CPPFM_JVM_STAMP}" ${CPPFM_JVM_API_SOURCES}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_with_timeout.py"
            VERBATIM)
        add_custom_target(cppfm_jvm_api_test ALL DEPENDS "${CPPFM_JVM_API_STAMP}")
    endif()
endif()

# The release executable is deliberately a single-file server bundle.  It
# carries the repository-owned registry/data inputs and compiled shadow API;
# RuntimeLayout extracts only missing files into the working server directory.
# Python is already used by the repository's generator/test gates, and the
# packer itself is platform-neutral (no shell, tar, or Unix utility calls).
# Native builds remain available without Python; the release `package` target
# is gated below so that a missing pack cannot be mistaken for a self-contained
# distribution.
if(Python3_Interpreter_FOUND)
    file(GLOB_RECURSE CPPFM_EMBED_ASSET_FILES CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
    set(CPPFM_EMBED_STUB_ARGS)
    set(CPPFM_EMBED_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_runtime.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/server.properties.example"
        ${CPPFM_EMBED_ASSET_FILES})
    if(TARGET cppfm_jvm_classes)
        list(APPEND CPPFM_EMBED_DEPENDS "${CPPFM_JVM_STAMP}")
        list(APPEND CPPFM_EMBED_STUB_ARGS
             --compile-stubs-dir "${CPPFM_JVM_COMPILE_STUB_DIR}")
    endif()
    add_custom_command(
        OUTPUT "${CPPFM_EMBEDDED_HEADER}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_runtime.py"
                --assets-dir "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                --classes-dir "${CPPFM_JVM_CLASSES_DIR}"
                ${CPPFM_EMBED_STUB_ARGS}
                --template "${CMAKE_CURRENT_SOURCE_DIR}/server.properties.example"
                --output "${CPPFM_EMBEDDED_HEADER}"
        DEPENDS ${CPPFM_EMBED_DEPENDS}
        VERBATIM)
    add_custom_target(cppfm_embedded_resources ALL
                      DEPENDS "${CPPFM_EMBEDDED_HEADER}")
else()
    message(WARNING
        "Python3 was not found; cppfm will use external assets and cannot be a self-contained one-file bundle")
endif()

set(CPPFM_JVM_NATIVE_SOURCES
    src/jvm/JavaObjectCache.cpp
    src/jvm/JvmRuntime.cpp
    src/jvm/ModRoutingTable.cpp
    src/jvm/NativeHandleTable.cpp)

function(cppfm_configure_jvm_target target)
    target_sources(${target} PRIVATE ${CPPFM_JVM_NATIVE_SOURCES})
    target_link_libraries(${target} PRIVATE ${CPPFM_PLATFORM_LIBS})
    if(JNI_FOUND)
        target_include_directories(${target} PRIVATE ${JNI_INCLUDE_DIRS})
        target_compile_definitions(${target} PRIVATE CPPFM_HAS_JNI=1
            CPPFM_DEFAULT_JVM_CLASSES="${CPPFM_DEFAULT_JVM_CLASSES_PATH}")
    endif()
endfunction()

set(CPPFM_SERVER_SOURCES
    src/core/NBT.cpp
    src/core/RuntimeLayout.cpp
    src/game/GameServer_tick.cpp
    src/game/GameServer_combat.cpp
    src/game/GameServer_items.cpp
    src/game/GameServer_world.cpp
    src/game/GameServer_session.cpp
    src/game/GameServer_core.cpp
    src/game/Recipes.cpp
    src/game/MenuInteraction.cpp
    src/game/Commands.cpp
    src/game/commands_chat.cpp
    src/game/commands_admin.cpp
    src/game/commands_world.cpp
    src/game/commands_player.cpp
    src/game/commands_data.cpp
    src/game/commands_misc.cpp
    src/game/commands_execute.cpp
    src/game/commands_scoreboard.cpp
    src/game/Containers.cpp
    src/physics/LightEngine.cpp
    src/physics/Fluids.cpp
    src/physics/Redstone.cpp
    src/physics/BlockTickScheduler.cpp
    src/game/WorldGen.cpp
    src/worldgen/DensityFunction.cpp
    src/worldgen/MultiNoise.cpp
    src/worldgen/StructurePlacer.cpp
    src/worldgen/ChunkGenerator.cpp
    src/worldgen/StructureManager.cpp
    src/game/WorldDataManager.cpp
    src/game/AiBrain.cpp
    src/game/Stats.cpp
    src/net/PacketBatcher.cpp
    src/game/BehaviorTree.cpp
    src/game/EntityData.cpp
    src/game/BossAI.cpp
    src/game/MenuLogic.cpp
    src/game/MobSpawner.cpp
    src/game/HungerManager.cpp
    src/game/CombatManager.cpp
    src/game/CombatShield.cpp
    src/game/FunctionEvaluator.cpp
)

add_executable(cppfm src/main.cpp ${CPPFM_SERVER_SOURCES})
target_include_directories(cppfm PRIVATE "${CPPFM_EMBEDDED_DIR}" src src/generated
    src/core)
target_link_libraries(cppfm PRIVATE Threads::Threads ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto
    ${CPPFM_PLATFORM_LIBS})
if(TARGET cppfm_embedded_resources)
    add_dependencies(cppfm cppfm_embedded_resources)
endif()
if(TARGET cppfm_jvm_classes)
    add_dependencies(cppfm cppfm_jvm_classes)
endif()
if(TARGET cppfm_jvm_fixture)
    add_dependencies(cppfm cppfm_jvm_fixture)
endif()
if(TARGET cppfm_jvm_manifest)
    add_dependencies(cppfm cppfm_jvm_manifest)
endif()

cppfm_configure_jvm_target(cppfm)

if(CPPFM_BUILD_TESTS)
enable_testing()
add_executable(test_golden tests/test_golden.cpp src/core/NBT.cpp
    src/game/WorldGen.cpp
    src/worldgen/DensityFunction.cpp src/worldgen/MultiNoise.cpp
    src/worldgen/StructurePlacer.cpp
    src/worldgen/ChunkGenerator.cpp src/worldgen/StructureManager.cpp src/game/WorldDataManager.cpp)
target_include_directories(test_golden PRIVATE src src/generated)
target_link_libraries(test_golden PRIVATE ZLIB::ZLIB OpenSSL::Crypto)

add_executable(test_native tests/TestClient.cpp tests/native_integration.cpp
    ${CPPFM_SERVER_SOURCES})
target_include_directories(test_native PRIVATE "${CPPFM_EMBEDDED_DIR}"
    src src/generated tests)
target_link_libraries(test_native PRIVATE Threads::Threads ZLIB::ZLIB OpenSSL::Crypto
    ${CPPFM_PLATFORM_LIBS})
if(TARGET cppfm_embedded_resources)
    add_dependencies(test_native cppfm_embedded_resources)
endif()

# self-contained: spawns the server itself on a random port
add_test(NAME native COMMAND test_native $<TARGET_FILE:cppfm>
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(native PROPERTIES TIMEOUT 300)

# plan28 D26: header-only ResetScore 0x49 wire + wildcard reset lock
add_executable(test_scoreboard_reset tests/test_scoreboard_reset.cpp)
target_include_directories(test_scoreboard_reset PRIVATE src src/generated)
add_test(NAME scoreboard_reset COMMAND test_scoreboard_reset)
set_tests_properties(scoreboard_reset PROPERTIES TIMEOUT 30)

add_executable(test_spec_wire tests/test_spec_wire.cpp
    src/core/NBT.cpp src/game/Stats.cpp
    src/game/WorldGen.cpp src/worldgen/DensityFunction.cpp src/worldgen/MultiNoise.cpp
    src/worldgen/StructurePlacer.cpp
    src/worldgen/ChunkGenerator.cpp src/worldgen/StructureManager.cpp src/game/WorldDataManager.cpp)
target_include_directories(test_spec_wire PRIVATE src src/generated)
target_link_libraries(test_spec_wire PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME spec_wire COMMAND test_spec_wire)
set_tests_properties(spec_wire PROPERTIES TIMEOUT 30)

add_executable(test_smoke_80 tests/TestClient.cpp tests/test_smoke_80.cpp
    ${CPPFM_SERVER_SOURCES})
target_include_directories(test_smoke_80 PRIVATE "${CPPFM_EMBEDDED_DIR}"
    src src/generated tests)
target_link_libraries(test_smoke_80 PRIVATE Threads::Threads ZLIB::ZLIB OpenSSL::Crypto
    ${CPPFM_PLATFORM_LIBS})
if(TARGET cppfm_embedded_resources)
    add_dependencies(test_smoke_80 cppfm_embedded_resources)
endif()
add_test(NAME smoke80 COMMAND test_smoke_80 $<TARGET_FILE:cppfm>
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(smoke80 PROPERTIES TIMEOUT 450)

# plan51 §1–2: generation-safe handle and selective routing invariants.
add_executable(test_jvm_handles
    tests/test_jvm_handles.cpp
    src/jvm/NativeHandleTable.cpp
    src/jvm/ModRoutingTable.cpp)
target_include_directories(test_jvm_handles PRIVATE src)
add_test(NAME jvm_handles COMMAND test_jvm_handles)
set_tests_properties(jvm_handles PROPERTIES TIMEOUT 30)

# plan51 §4/7/8/14/15/20–23: native ABI, generation, and dynamic-routing
# invariants.  This test deliberately avoids starting a JVM so it remains
# usable on build hosts without a Java fixture.
add_executable(test_jvm_native_bridge
    tests/test_jvm_native_bridge.cpp
    src/jvm/NativeHandleTable.cpp
    src/jvm/ModRoutingTable.cpp)
target_include_directories(test_jvm_native_bridge PRIVATE src)
add_test(NAME jvm_native_bridge COMMAND test_jvm_native_bridge)
set_tests_properties(jvm_native_bridge PROPERTIES TIMEOUT 30)

if(TARGET cppfm_jvm_fixture AND JNI_FOUND)
    add_test(NAME jvm_runtime
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_runtime_smoke.py"
                --binary $<TARGET_FILE:cppfm>
                --classes "${CPPFM_JVM_CLASSES_DIR}"
                --mods "${CMAKE_CURRENT_BINARY_DIR}/jvm/fixture-mods")
    set_tests_properties(jvm_runtime PROPERTIES TIMEOUT 60
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
if(TARGET cppfm_jvm_transformer AND CPPFM_JAVA)
    add_test(NAME jvm_transformer
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_with_timeout.py"
                --timeout 60 -- "${CPPFM_JAVA}" -ea
                -cp "${CPPFM_JVM_CLASSES_DIR}${CPPFM_JAVA_CLASSPATH_SEPARATOR}${CPPFM_JVM_TRANSFORMER_DIR}"
                cppfm.transformer_fixture.TransformerContractTest)
    set_tests_properties(jvm_transformer PROPERTIES TIMEOUT 60
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
if(TARGET cppfm_jvm_access_widener AND CPPFM_JAVA)
    add_test(NAME jvm_access_widener
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_with_timeout.py"
                --timeout 120 -- "${CPPFM_JAVA}" -ea
                -cp "${CPPFM_JVM_ACCESS_WIDENER_DIR}"
                cppfm.accesswidener_fixture.AccessWidenerContractTest)
    set_tests_properties(jvm_access_widener PROPERTIES TIMEOUT 150
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
if(TARGET cppfm_jvm_api_test AND CPPFM_JAVA)
    add_test(NAME jvm_api
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_with_timeout.py"
                --timeout 60 -- "${CPPFM_JAVA}" -ea
                -cp "${CPPFM_JVM_CLASSES_DIR}${CPPFM_JAVA_CLASSPATH_SEPARATOR}${CPPFM_JVM_COMPILE_STUB_DIR}${CPPFM_JAVA_CLASSPATH_SEPARATOR}${CPPFM_JVM_API_DIR}"
                cppfm.api_fixture.ApiSurfaceContractTest)
    set_tests_properties(jvm_api PROPERTIES TIMEOUT 90
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
if(TARGET cppfm_jvm_classes AND TARGET cppfm_jvm_manifest AND JNI_FOUND
   AND Python3_Interpreter_FOUND)
    add_test(NAME jvm_compatibility
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_compatibility_smoke.py"
                --binary $<TARGET_FILE:cppfm>
                --classes "${CPPFM_JVM_CLASSES_DIR}"
                --manifest "${CPPFM_JVM_MANIFEST}")
    set_tests_properties(jvm_compatibility PROPERTIES TIMEOUT 300
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    # plan51 §26/27: the complete directory-mod corpus is a separate gate
    # from the two-file bootstrap fixture above.  It owns its temporary
    # staging directory and each server process, so this test can exercise
    # all 25 cases without changing the production mod directory.
    add_test(NAME jvm_corpus
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_compatibility_smoke.py"
                --binary $<TARGET_FILE:cppfm>
                --classes "${CPPFM_JVM_CLASSES_DIR}"
                --manifest "${CPPFM_JVM_MANIFEST}"
                --corpus "${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_fixture/corpus"
                --report-output "${CMAKE_CURRENT_BINARY_DIR}/jvm/corpus-report.json"
                --evidence-output "${CMAKE_CURRENT_BINARY_DIR}/jvm/corpus-evidence.log")
    set_tests_properties(jvm_corpus PROPERTIES TIMEOUT 300
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
if(TARGET cppfm_jvm_manifest)
    add_test(NAME jvm_manifest
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_shadow.py"
                --check --input "${CPPFM_JVM_SHADOW_SPEC}" --output "${CPPFM_JVM_MANIFEST}")
    set_tests_properties(jvm_manifest PROPERTIES TIMEOUT 30
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
if(TARGET cppfm_jvm_manifest AND Python3_Interpreter_FOUND)
    add_test(NAME jvm_contract_audit
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/plan51_contract_audit.py"
                --root "${CMAKE_CURRENT_SOURCE_DIR}"
                --manifest "${CPPFM_JVM_MANIFEST}")
    set_tests_properties(jvm_contract_audit PROPERTIES TIMEOUT 30
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
if(CPPFM_JAVAC AND CPPFM_JAVA AND Python3_Interpreter_FOUND)
    add_test(NAME shadow_abi
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/shadow_abi/test_shadow_abi.py"
                --repo "${CMAKE_CURRENT_SOURCE_DIR}"
                --javac "${CPPFM_JAVAC}" --java "${CPPFM_JAVA}")
    set_tests_properties(shadow_abi PROPERTIES TIMEOUT 240
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

cppfm_configure_jvm_target(test_native)
cppfm_configure_jvm_target(test_smoke_80)

# plan43 B1+B2 live-server tests (W-01..W-07, W-12; see docs/VERIFICATION.md) — TestClient + cppfm
add_executable(test_plan43
    tests/TestClient.cpp
    tests/test_plan43.cpp
    src/core/NBT.cpp)
target_include_directories(test_plan43 PRIVATE src src/generated tests)
target_link_libraries(test_plan43 PRIVATE Threads::Threads ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME plan43 COMMAND test_plan43 $<TARGET_FILE:cppfm>
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(plan43 PROPERTIES TIMEOUT 300)

# plan46 §1 (O-13/W-14/W-16) flood/defense: unit (budgets/throttle/gate) +
# live-server (fork cppfm, raw sockets + TestClient) A1–A8 scenarios.
add_executable(test_flood_net
    tests/TestClient.cpp
    tests/test_flood_net.cpp
    src/core/NBT.cpp)
target_include_directories(test_flood_net PRIVATE src src/generated tests)
target_link_libraries(test_flood_net PRIVATE Threads::Threads ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME flood_net COMMAND test_flood_net $<TARGET_FILE:cppfm>
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(flood_net PROPERTIES TIMEOUT 300)

add_executable(test_fuzz tests/test_fuzz.cpp
    src/core/NBT.cpp)
target_include_directories(test_fuzz PRIVATE src src/generated)
target_link_libraries(test_fuzz PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME fuzz COMMAND test_fuzz)
set_tests_properties(fuzz PROPERTIES TIMEOUT 30)

add_executable(test_core_safety tests/test_core_safety.cpp
    src/core/NBT.cpp)
target_include_directories(test_core_safety PRIVATE src src/generated)
target_link_libraries(test_core_safety PRIVATE ZLIB::ZLIB OpenSSL::Crypto Threads::Threads)
add_test(NAME core_safety COMMAND test_core_safety)
set_tests_properties(core_safety PROPERTIES TIMEOUT 30)

# Runtime bootstrap contract: a clean directory receives the standard server
# tree and embedded resources without overwriting user-owned assets.
add_executable(test_runtime_layout
    tests/test_runtime_layout.cpp
    src/core/RuntimeLayout.cpp)
target_include_directories(test_runtime_layout PRIVATE
    "${CPPFM_EMBEDDED_DIR}" src src/generated)
target_link_libraries(test_runtime_layout PRIVATE Threads::Threads ZLIB::ZLIB)
if(TARGET cppfm_embedded_resources)
    add_dependencies(test_runtime_layout cppfm_embedded_resources)
endif()
add_test(NAME runtime_layout COMMAND test_runtime_layout)
set_tests_properties(runtime_layout PROPERTIES TIMEOUT 30)

# Version-pinned Play S→C ID locks plus selected field/order vectors. This is
# broad wire coverage, not an exhaustive proof of every packet payload branch.
add_executable(test_wire_full tests/test_wire_full.cpp
    src/core/NBT.cpp src/game/Stats.cpp
    src/game/WorldGen.cpp src/worldgen/DensityFunction.cpp src/worldgen/MultiNoise.cpp
    src/worldgen/StructurePlacer.cpp
    src/worldgen/ChunkGenerator.cpp src/worldgen/StructureManager.cpp src/game/WorldDataManager.cpp)
target_include_directories(test_wire_full PRIVATE src src/generated)
target_link_libraries(test_wire_full PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME wire_full COMMAND test_wire_full)
set_tests_properties(wire_full PROPERTIES TIMEOUT 30)

# plan45 B6: remaining fromClient (W-05/W-08/W-09/W-10/W-11/W-13) + G-13 network side
add_executable(test_wire_b6 tests/test_wire_b6.cpp
    src/core/NBT.cpp
    src/game/Recipes.cpp src/game/Containers.cpp src/game/MenuInteraction.cpp)
target_include_directories(test_wire_b6 PRIVATE src src/generated)
target_link_libraries(test_wire_b6 PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME wire_b6 COMMAND test_wire_b6)
set_tests_properties(wire_b6 PROPERTIES TIMEOUT 60)

# plan36 soak 2h nightly (300s dry + 7200s full) — B-06
if(Python3_Interpreter_FOUND)
    add_test(NAME soak2h
        COMMAND "${Python3_EXECUTABLE}" tests/soak_test.py
                --duration 7200 --binary $<TARGET_FILE:cppfm>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(soak2h PROPERTIES TIMEOUT 8000 LABELS nightly)
endif()

add_executable(test_gameplay_full tests/test_gameplay_full.cpp
    src/core/NBT.cpp
    src/physics/Redstone.cpp
    src/game/MenuInteraction.cpp src/game/Containers.cpp src/game/Recipes.cpp
    src/physics/BlockTickScheduler.cpp
    src/game/CombatShield.cpp
    src/worldgen/DensityFunction.cpp src/worldgen/MultiNoise.cpp src/worldgen/StructurePlacer.cpp src/worldgen/ChunkGenerator.cpp src/worldgen/StructureManager.cpp src/game/WorldGen.cpp src/game/WorldDataManager.cpp)
target_include_directories(test_gameplay_full PRIVATE src src/generated)
target_link_libraries(test_gameplay_full PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME gameplay_full COMMAND test_gameplay_full)
set_tests_properties(gameplay_full PROPERTIES TIMEOUT 60)

# Focused 1.21.4 menu behavior: enchanting offers/consumption and the
# model-safe crafter redstone craft/output transfer path.
add_executable(test_menu_logic tests/test_menu_logic.cpp
    src/core/NBT.cpp
    src/game/MenuLogic.cpp src/game/MenuInteraction.cpp
    src/game/Containers.cpp src/game/Recipes.cpp)
target_include_directories(test_menu_logic PRIVATE src src/generated)
target_link_libraries(test_menu_logic PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME menu_logic COMMAND test_menu_logic)
set_tests_properties(menu_logic PROPERTIES TIMEOUT 60 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# plan45 G-11 — seed parity L1/L2 (independent Python hand-calc + determinism)
add_executable(test_seed_parity tests/test_seed_parity.cpp
    src/worldgen/DensityFunction.cpp src/worldgen/MultiNoise.cpp src/worldgen/StructurePlacer.cpp src/worldgen/StructureManager.cpp)
target_include_directories(test_seed_parity PRIVATE src src/generated)
add_test(NAME seed_parity COMMAND test_seed_parity)
set_tests_properties(seed_parity PROPERTIES TIMEOUT 120)

# plan44 G-02/G-03 — hardness 1095 lock + mining calculator (generated worktree)
add_executable(test_block_hardness_full tests/test_block_hardness_full.cpp)
target_include_directories(test_block_hardness_full PRIVATE src tests)
add_test(NAME block_hardness_full COMMAND test_block_hardness_full)
set_tests_properties(block_hardness_full PROPERTIES TIMEOUT 60)

# plan44 G-12: redstone engine verification (World + RedstoneEngine public API) — restored after merge conflict
add_executable(test_redstone_engine_full tests/test_redstone_engine_full.cpp
    src/core/NBT.cpp
    src/physics/Redstone.cpp
    src/game/MenuInteraction.cpp src/game/Containers.cpp src/game/Recipes.cpp
    src/worldgen/DensityFunction.cpp src/worldgen/MultiNoise.cpp src/worldgen/StructurePlacer.cpp src/worldgen/ChunkGenerator.cpp src/worldgen/StructureManager.cpp src/game/WorldGen.cpp src/game/WorldDataManager.cpp)
target_include_directories(test_redstone_engine_full PRIVATE src src/generated)
target_link_libraries(test_redstone_engine_full PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME redstone_engine_full COMMAND test_redstone_engine_full)
set_tests_properties(redstone_engine_full PROPERTIES TIMEOUT 60)

# Fluid collision, source conversion, and waterlogging regression coverage.
add_executable(test_fluids tests/test_fluids.cpp
    src/core/NBT.cpp
    src/physics/Fluids.cpp
    src/game/WorldGen.cpp
    src/worldgen/DensityFunction.cpp src/worldgen/MultiNoise.cpp
    src/worldgen/StructurePlacer.cpp src/worldgen/ChunkGenerator.cpp
    src/worldgen/StructureManager.cpp src/game/WorldDataManager.cpp)
target_include_directories(test_fluids PRIVATE src src/generated)
target_link_libraries(test_fluids PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME fluids COMMAND test_fluids)
set_tests_properties(fluids PROPERTIES TIMEOUT 60)

# plan44 G-05: mob stats 149 lock — restored after merge conflict
add_executable(test_mob_stats_full tests/test_mob_stats_full.cpp)
target_include_directories(test_mob_stats_full PRIVATE src src/generated)
add_test(NAME mob_stats_full COMMAND test_mob_stats_full)
set_tests_properties(mob_stats_full PROPERTIES TIMEOUT 30)

add_executable(test_mining_full tests/test_mining_full.cpp)
target_include_directories(test_mining_full PRIVATE src tests)
add_test(NAME mining_full COMMAND test_mining_full)
set_tests_properties(mining_full PROPERTIES TIMEOUT 60)

# Static test-quality gate: reject unconditional PASS and liveness-only
# fallbacks.  Keep this independent from any one smoke test so every future
# regression source receives the same review rule.
if(Python3_Interpreter_FOUND)
    add_test(NAME quality_audit
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/quality_audit.py"
                --root "${CMAKE_SOURCE_DIR}/tests"
                --source-root "${CMAKE_SOURCE_DIR}/src"
                --python-root "${CMAKE_SOURCE_DIR}/tests"
                --python-root "${CMAKE_SOURCE_DIR}/tools")
    set_tests_properties(quality_audit PROPERTIES TIMEOUT 10
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_test(NAME tautology_lint
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/tautology_lint.py"
                --strict)
    set_tests_properties(tautology_lint PROPERTIES TIMEOUT 10
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_test(NAME mcproto_framing
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_mcproto_framing.py")
    set_tests_properties(mcproto_framing PROPERTIES TIMEOUT 30
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

# plan52 P1: fail-closed preflight for arbitrary Fabric/JVM mod linkage.
# This test is independent of the optional JDK/JNI runtime and uses only the
# Python standard library, so it remains available on native-only hosts.
if(Python3_Interpreter_FOUND)
    add_test(NAME mod_linkage
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_scan_mod_linkage.py")
    set_tests_properties(mod_linkage PROPERTIES TIMEOUT 60
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # Real-mod corpus report contract: exercise the offline/strict decision
    # matrix without requiring the ignored JAR cache or network access.
    add_test(NAME real_mod_harness
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/real_mod_corpus/test_harness.py")
    set_tests_properties(real_mod_harness PROPERTIES TIMEOUT 120
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # plan52 P0: the wider Modrinth candidate probe has a separate offline
    # contract gate.  Real JAR downloads and JVM runs remain explicit evidence
    # commands because a clean checkout may not have the ignored cache.
    add_test(NAME real_mod_candidates_harness
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/real_mod_corpus/test_candidate_probe.py")
    set_tests_properties(real_mod_candidates_harness PROPERTIES TIMEOUT 120
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

# plan41 C-09 bench and C-12 bot verification are optional Python tests.  Use
# CMake's resolved interpreter so Windows/macOS installations do not need a
# shell alias named `python3`; omit these tests on a native-only host.
if(Python3_Interpreter_FOUND)
    add_test(NAME bench
        COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/bench_chunk_gen.py
                --view-distance 32 --chunks 100 --dry --strict)
    set_tests_properties(bench PROPERTIES TIMEOUT 30 LABELS bench)

    add_test(NAME multi_client
        COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tests/multi_client_test.py
                --binary $<TARGET_FILE:cppfm>)
    set_tests_properties(multi_client PROPERTIES TIMEOUT 120
        LABELS "multi;network" WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_test(NAME bot_smoke
        COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tests/bot_smoke.py
                --duration 30 --binary $<TARGET_FILE:cppfm>)
    set_tests_properties(bot_smoke PROPERTIES TIMEOUT 120
        LABELS "bot;smoke" WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_test(NAME soak_bot
        COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/soak_bot.py
                --duration 300 --binary $<TARGET_FILE:cppfm>)
    set_tests_properties(soak_bot PROPERTIES TIMEOUT 400
        LABELS "soak;nightly" WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()


# plan41 C-11 recipes mirror/offset strict (20+ cases) — inventory
add_executable(test_recipes_mirror
    tests/test_recipes_mirror.cpp
    src/core/NBT.cpp
    src/game/Recipes.cpp)
target_include_directories(test_recipes_mirror PRIVATE src src/generated)
target_link_libraries(test_recipes_mirror PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME recipes_mirror COMMAND test_recipes_mirror WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(recipes_mirror PROPERTIES TIMEOUT 30 LABELS inventory)

# plan46 §2 recovery (O-07/O-08): offline world-integrity checker
add_executable(check_world
    tools/check_world.cpp
    src/core/NBT.cpp)
target_include_directories(check_world PRIVATE src src/generated)
target_link_libraries(check_world PRIVATE ZLIB::ZLIB)

# plan46 §2 recovery (O-07/O-08): loadWithRecovery matrix + quarantine + session.lock
add_executable(test_recovery tests/test_recovery.cpp
    src/core/NBT.cpp
    src/game/WorldGen.cpp
    src/worldgen/DensityFunction.cpp src/worldgen/MultiNoise.cpp
    src/worldgen/StructurePlacer.cpp
    src/worldgen/ChunkGenerator.cpp src/worldgen/StructureManager.cpp
    src/game/WorldDataManager.cpp)
target_include_directories(test_recovery PRIVATE src src/generated)
target_link_libraries(test_recovery PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME recovery COMMAND test_recovery WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(recovery PROPERTIES TIMEOUT 120 LABELS recovery)

# plan46 §2 (O-09): 5x concurrent RCON + 10x wrong-pass harness (in-process)
add_executable(test_rcon_multi tests/test_rcon_multi.cpp)
target_include_directories(test_rcon_multi PRIVATE src src/generated)
target_link_libraries(test_rcon_multi PRIVATE Threads::Threads)
add_test(NAME rcon_multi COMMAND test_rcon_multi WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(rcon_multi PROPERTIES TIMEOUT 60 LABELS recovery)

# plan44 G-05 mob attributes 149 species (world; see docs/SPEC_GAMEPLAY.md) — header-only, reads docs/mob_stats_149.csv

endif() # CPPFM_BUILD_TESTS

# The install tree contains exactly the executable.  All repository-owned
# assets and shadow classes must already be inside it; CPack therefore produces
# a one-file ZIP suitable for copying to a fresh MinecraftServer directory.
# Run this preflight inside CPack rather than making Python a global
# configure-time requirement: native fallback builds must remain usable on
# hosts without Python/JNI, while `package` itself must fail closed.
set(CPPFM_PACKAGE_ASSET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/assets")
set(CPPFM_PACKAGE_REQUIRED_ASSET_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/registry")
if(Python3_Interpreter_FOUND AND TARGET cppfm_embedded_resources)
    set(CPPFM_PACKAGE_EMBED_READY TRUE)
else()
    set(CPPFM_PACKAGE_EMBED_READY FALSE)
endif()
set(CPPFM_PACKAGE_PREFLIGHT_SCRIPT
    "${CMAKE_CURRENT_BINARY_DIR}/cppfm_package_preflight.cmake")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_self_contained_package.cmake.in"
    "${CPPFM_PACKAGE_PREFLIGHT_SCRIPT}" @ONLY)

install(TARGETS cppfm
        RUNTIME DESTINATION .
        BUNDLE DESTINATION .)
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "cppfabricmc")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPPFM_PACKAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
if(CPPFM_PACKAGE_ARCH STREQUAL "")
    set(CPPFM_PACKAGE_ARCH "unknown")
endif()
set(CPPFM_PACKAGE_FILE_NAME
    "cppfabricmc-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CPPFM_PACKAGE_ARCH}")
set(CPACK_PACKAGE_FILE_NAME "${CPPFM_PACKAGE_FILE_NAME}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_INSTALL_SCRIPTS "${CPPFM_PACKAGE_PREFLIGHT_SCRIPT}")
set(CPPFM_PACKAGE_EXECUTABLE "cppfm")
if(WIN32)
    set(CPPFM_PACKAGE_EXECUTABLE "cppfm.exe")
endif()
include(CPack)
set(CPPFM_PACKAGE_ARCHIVE
    "${CPACK_PACKAGE_DIRECTORY}/${CPPFM_PACKAGE_FILE_NAME}.zip")

# This is intentionally separate from the native package lifecycle test.  It
# extracts the exact CPack ZIP into a directory containing no checkout assets,
# starts the binary without a --jvm-classes override, and requires strict JVM
# startup evidence.  Do not register it when JNI/classes are unavailable: the
# native fallback contract remains valid and is covered by the native gates.
if(CPPFM_BUILD_TESTS AND Python3_Interpreter_FOUND AND
   TARGET cppfm_jvm_classes AND JNI_FOUND)
    add_test(NAME package_jvm_smoke
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/package_jvm_smoke.py"
                --package
                "${CPPFM_PACKAGE_ARCHIVE}"
                --executable-name "${CPPFM_PACKAGE_EXECUTABLE}")
    set_tests_properties(package_jvm_smoke PROPERTIES TIMEOUT 300
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        LABELS "package;jvm")
endif()
