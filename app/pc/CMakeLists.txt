# 要求 CMake 版本至少 3.16，版本太低会配置失败
cmake_minimum_required(VERSION 3.16)

project(fpga_learn VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network OpenGLWidgets Quick QuickWidgets Quick3D)

qt_add_executable(fpga_learn
    main.cpp
    mainwindow.h
    mainwindow.cpp
    capturewindow.h
    capturewindow.cpp
    gatewaywaveformclient.h
    gatewaywaveformclient.cpp
    motorglwidget.h
    motorglwidget.cpp
    motorview3d.h
    motorview3d.cpp
    reportwindow.h
    reportwindow.cpp
    waveformwidget.h
    waveformwidget.cpp
    aiclient.h
    aiclient.cpp
    boardalertclient.h
    boardalertclient.cpp
    serialmodbusclient.h
    serialmodbusclient.cpp
    serialmodbusworker.h
    serialmodbusworker.cpp
    showapiserver.h
    showapiserver.cpp
    fpga_learn.qrc
)

target_link_libraries(fpga_learn PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::OpenGLWidgets
    Qt6::Quick
    Qt6::QuickWidgets
    Qt6::Quick3D
)

install(TARGETS fpga_learn RUNTIME DESTINATION .)
install(DIRECTORY assets qml DESTINATION .)

add_executable(capture_test EXCLUDE_FROM_ALL
    tests/capture_test.cpp
    capturewindow.cpp capturewindow.h
    waveformwidget.cpp waveformwidget.h
)
target_include_directories(capture_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(capture_test PRIVATE Qt6::Widgets Qt6::Network)

add_executable(serialmodbus_test EXCLUDE_FROM_ALL
    tests/serialmodbus_test.cpp
    serialmodbusclient.cpp serialmodbusclient.h
)
target_include_directories(serialmodbus_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(serialmodbus_test PRIVATE Qt6::Core)

if(WIN32)
    target_link_libraries(fpga_learn PRIVATE opengl32)
endif()

add_custom_command(TARGET fpga_learn POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:fpga_learn>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/qml
            $<TARGET_FILE_DIR:fpga_learn>/qml
    COMMENT "Copy assets + qml next to fpga_learn.exe"
)

if(WIN32)
    get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake}" DIRECTORY)
    set(_windeployqt "${_qt_bin_dir}/windeployqt.exe")
    if(EXISTS "${_windeployqt}")
        add_custom_command(TARGET fpga_learn POST_BUILD
            COMMAND "${_windeployqt}"
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                    --quick
                    --no-translations
                    "$<TARGET_FILE:fpga_learn>"
            COMMENT "Deploy Qt Quick3D QML plugins (windeployqt)"
        )
    endif()
endif()
