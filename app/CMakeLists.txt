find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

# The window and its panels, as a library: cutreel links it behind its
# main(), and the GUI tests in app/tests link it in front of Catch2's. A test
# cannot drive a window that only exists inside an executable.
add_library(zaro_app_ui STATIC
    ActionRouter.cpp
    ActionRouter.h
    AudioStrip.cpp
    AudioStrip.h
    ChannelPanel.cpp
    ChannelPanel.h
    ClipStrip.cpp
    ClipStrip.h
    ColorPalette.cpp
    ColorPalette.h
    ColorWheel.cpp
    ColorWheel.h
    CurveEditor.cpp
    CurveEditor.h
    DeliverPanel.cpp
    DeliverPanel.h
    Document.cpp
    Document.h
    EffectControls.cpp
    EffectControls.h
    ExportDialog.cpp
    ExportDialog.h
    FrameGrab.h
    FrameThumb.cpp
    FrameThumb.h
    GalleryPanel.cpp
    GalleryPanel.h
    GradeNodes.cpp
    GradeNodes.h
    Hotkeys.cpp
    Hotkeys.h
    KeyboardMap.cpp
    KeyboardMap.h
    HueBand.cpp
    HueBand.h
    Icons.cpp
    Icons.h
    LoudnessPanel.cpp
    LoudnessPanel.h
    MaskOverlay.cpp
    MediaBrowser.cpp
    MediaDrag.cpp
    MediaDrag.h
    MixerPanel.cpp
    MixerPanel.h
    PlaybackController.cpp
    PlaybackController.h
    PreviewWindow.cpp
    PreviewWindow.h
    ProgramMonitor.cpp
    ProgramMonitor.h
    ProjectBin.cpp
    ProjectBin.h
    Say.cpp
    ScopesPanel.cpp
    ScopesPanel.h
    SourceMonitor.cpp
    SourceMonitor.h
    StemsPanel.cpp
    StemsPanel.h
    SupportButton.cpp
    SupportButton.h
    Theme.cpp
    Theme.h
    TitleOverlay.cpp
    TitleOverlay.h
    TitlePresets.cpp
    TitlePresets.h
    ThumbnailCache.cpp
    ThumbnailCache.h
    TimelineWidget.cpp
    TimelineWidget.h
    Transcript.cpp
    ViewerOverlay.cpp
    ViewerOverlay.h

    chrome/Bars.cpp
    chrome/Bars.h
    chrome/Choices.cpp
    chrome/Choices.h
    chrome/FlowLayout.cpp
    chrome/FlowLayout.h
    chrome/Menus.cpp
    chrome/Menus.h
    chrome/Widgets.cpp
    chrome/Widgets.h

    commands/Analysis.cpp
    commands/Analysis.h
    commands/Context.h
    commands/Media.cpp
    commands/Media.h
    commands/Multicam.cpp
    commands/Multicam.h
    commands/Music.cpp
    commands/Music.h
    commands/Review.cpp
    commands/Review.h
    commands/Structure.cpp
    commands/Structure.h
    commands/Templates.cpp
    commands/Templates.h
)

target_link_libraries(zaro_app_ui
    PUBLIC
        zaro::version
        zaro::ui_core
        zaro::platform_qrhi
        zaro::platform_qtext
        zaro::platform_ffmpeg
        zaro::platform_sdl
        zaro::warnings
        Qt6::Widgets
)

target_include_directories(zaro_app_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(zaro_app_ui PROPERTIES AUTOMOC ON)

# --- Branding -------------------------------------------------------------------
# One directory of artwork, three platforms that each want it in their own
# shape. See resources/branding/README.md for what every file is for.
set(ZARO_BRANDING_DIR "${CMAKE_SOURCE_DIR}/resources/branding")

add_executable(cutreel main.cpp)
target_link_libraries(cutreel PRIVATE zaro_app_ui zaro::warnings)
set_target_properties(cutreel PROPERTIES AUTOMOC ON)

# The window icon, on every platform. Compiled in rather than loaded from a
# file beside the binary, which is a file that goes missing the first time
# somebody moves the executable.
#
# On the executable and not on zaro_app_ui: a Qt resource inside a static
# library is only registered if something drags its initialiser in, and a
# library nobody references by name from main is exactly the case where the
# linker drops it. On the executable there is nothing to drop.
qt_add_resources(cutreel "branding"
    PREFIX "/branding"
    BASE "${ZARO_BRANDING_DIR}"
    FILES
        "${ZARO_BRANDING_DIR}/CutReel-16.png"
        "${ZARO_BRANDING_DIR}/CutReel-32.png"
        "${ZARO_BRANDING_DIR}/CutReel-48.png"
        "${ZARO_BRANDING_DIR}/CutReel-64.png"
        "${ZARO_BRANDING_DIR}/CutReel-128.png"
        "${ZARO_BRANDING_DIR}/CutReel-256.png"
        "${ZARO_BRANDING_DIR}/CutReel-512.png")

# The executable's own icon, which is a different thing from the window's: it
# is what Explorer, the taskbar and the desktop shortcut the MSI installs all
# display, and none of them ever runs the program to ask Qt.
#
# The document icon rides along in the same binary. A .cutreel file is not an
# executable and cannot carry an icon of its own, so the association the MSI
# writes points Explorer back at an icon resource here -- see
# app/cutreel.rc.in for the id/index contract that association depends on,
# and cmake/wix-patch.xml for the association itself. Kept in the executable
# rather than installed as a loose .ico beside it: a file a user can delete or
# move is a file that turns every project on their disk blank.
if(WIN32)
    set(ZARO_ICON_ICO "${ZARO_BRANDING_DIR}/CutReel.ico")
    set(ZARO_DOCUMENT_ICO "${ZARO_BRANDING_DIR}/CutReel-Document.ico")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cutreel.rc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/cutreel.rc" @ONLY)
    target_sources(cutreel PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cutreel.rc")
endif()

# The bundle icon. The file has to be inside Contents/Resources and the plist
# key names it without a path, so both halves are set here; naming one without
# the other is a bundle that shows the generic application icon.
if(APPLE)
    target_sources(cutreel PRIVATE "${ZARO_BRANDING_DIR}/CutReel.icns")
    set_source_files_properties("${ZARO_BRANDING_DIR}/CutReel.icns"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set_target_properties(cutreel PROPERTIES MACOSX_BUNDLE_ICON_FILE "CutReel.icns")
endif()


# --- Installation -------------------------------------------------------------
# A bundle on macOS and a windowed executable on Windows, because that is what
# the deployment tools below expect to find and what a double-click on either
# platform expects to launch. On Linux it is a plain binary either way.
set_target_properties(cutreel PROPERTIES
    MACOSX_BUNDLE ON
    WIN32_EXECUTABLE ON)

include(GNUInstallDirs)
install(TARGETS cutreel
    RUNTIME_DEPENDENCY_SET zaro_runtime_deps
    BUNDLE  DESTINATION .                            COMPONENT runtime
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"    COMPONENT runtime)

# Qt's own libraries and plugins are not on a user's machine, so the archive has
# to carry them. windeployqt and macdeployqt know which ones; nothing else does.
if(WIN32 OR APPLE)
    qt_generate_deploy_app_script(
        TARGET cutreel
        OUTPUT_SCRIPT zaro_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT "${zaro_deploy_script}" COMPONENT runtime)
endif()

# --- Linux ---------------------------------------------------------------------
# Linux has no deployment tool of Qt's, so the two halves are done by hand: the
# shared libraries come from the runtime dependency set in cmake/Packaging.cmake,
# which resolves them the way the loader would, and the plugins come from here,
# because no dependency walk can see them. Qt opens a platform plugin by name at
# startup; nothing links it, so nothing points at it.
if(UNIX AND NOT APPLE)
    set_target_properties(cutreel PROPERTIES INSTALL_RPATH "${ZARO_PRIVATE_RPATH}")

    # xcb is what a desktop session wants and offscreen is what a render farm,
    # a container and an ssh session want -- zaro-render selects the second for
    # itself when there is no display. Missing either one is a program that
    # aborts on startup with "could not load the Qt platform plugin", which
    # says nothing about a file having been left out of a package.
    #
    # Added to the same dependency set as the executables: a plugin has
    # libraries of its own, and on Linux they are not always ones the
    # application already pulled in.
    set(zaro_qt_plugins "")
    foreach(plugin
            Qt6::QXcbIntegrationPlugin
            Qt6::QOffscreenIntegrationPlugin
            Qt6::QMinimalIntegrationPlugin)
        if(TARGET ${plugin})
            list(APPEND zaro_qt_plugins ${plugin})
        endif()
    endforeach()
    if(NOT zaro_qt_plugins)
        message(WARNING
            "No Qt platform plugins found to install; the packaged cutreel "
            "will not start on a machine without a Qt of its own.")
    else()
        install(IMPORTED_RUNTIME_ARTIFACTS ${zaro_qt_plugins}
            RUNTIME_DEPENDENCY_SET zaro_runtime_deps
            LIBRARY DESTINATION "${ZARO_PRIVATE_LIBDIR}/plugins/platforms"
                    COMPONENT runtime)
    endif()

    # How the xcb plugin gets an OpenGL context, which in Qt 6 is a plugin of
    # its own rather than part of the platform plugin. Nothing links these
    # either, and leaving them out does not stop the application starting: it
    # starts, finds no way to make a context, and every QRhiWidget in it stays
    # blank. That is the whole monitor and the whole timeline preview, and it
    # is reported as
    #
    #   QXcbIntegration: Cannot create platform OpenGL context, neither GLX
    #   nor EGL are enabled
    #   QRhiGles2: Failed to create context
    #   QRhiWidget: No QRhi
    #
    # which names no missing file, and is what shipped. Both are installed
    # because which one is usable is the machine's decision, not ours: GLX is
    # the one X11 with a traditional driver offers, EGL the one an
    # EGL-first stack or XWayland-with-EGL offers, and Qt picks between them
    # at startup.
    set(zaro_qt_gl_plugins "")
    foreach(plugin Qt6::QXcbGlxIntegrationPlugin Qt6::QXcbEglIntegrationPlugin)
        if(TARGET ${plugin})
            list(APPEND zaro_qt_gl_plugins ${plugin})
        endif()
    endforeach()
    if(zaro_qt_gl_plugins)
        install(IMPORTED_RUNTIME_ARTIFACTS ${zaro_qt_gl_plugins}
            RUNTIME_DEPENDENCY_SET zaro_runtime_deps
            LIBRARY DESTINATION "${ZARO_PRIVATE_LIBDIR}/plugins/xcbglintegrations"
                    COMPONENT runtime)
    elseif(TARGET Qt6::QXcbIntegrationPlugin)
        # Fatal rather than a warning, and only in the combination that is
        # certainly broken: shipping the xcb plugin with no way for it to
        # reach a GPU. A warning is what this was, in effect, and a warning in
        # a green build log is how a package that cannot draw gets released.
        message(FATAL_ERROR
            "Qt supplies the xcb platform plugin but neither of its OpenGL "
            "integration plugins (QXcbGlxIntegrationPlugin, "
            "QXcbEglIntegrationPlugin). A package built from this Qt would "
            "start and then draw nothing. Install a Qt built with GLX or EGL "
            "support for xcb.")
    endif()

    # Image formats, for anything the interface draws that is not a PNG -- PNG
    # is built into Qt Gui and needs no plugin.
    foreach(plugin Qt6::QJpegPlugin Qt6::QSvgPlugin)
        if(TARGET ${plugin})
            install(IMPORTED_RUNTIME_ARTIFACTS ${plugin}
                RUNTIME_DEPENDENCY_SET zaro_runtime_deps
                LIBRARY DESTINATION "${ZARO_PRIVATE_LIBDIR}/plugins/imageformats"
                        COMPONENT runtime)
        endif()
    endforeach()

    # And how anything in bin/ finds those plugins. Qt reads qt.conf from beside
    # the executable before it does anything else, and a relative Prefix in it
    # is resolved against that same directory -- so this one file serves the app
    # and all seven tools, wherever the package was unpacked or installed.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/qt.conf"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT runtime)

    # --- Desktop integration ----------------------------------------------------
    # Two things a .deb has to carry that a tarball does not, because a tarball
    # is a directory somebody unpacks and looks inside, and an installed package
    # is a name somebody types and an icon somebody clicks. The name itself is
    # not one of them any more -- the executable is called cutreel, the same as
    # the package, so `apt install ./cutreel...` is followed by `cutreel` and
    # that is the end of it.
    #
    # The first is the launcher entry, which is what puts CutReel in the
    # applications menu and in a search for "video". Without it an installed
    # package is reachable only from a terminal, which for a video editor is
    # most of the way to not being installed at all.
    install(FILES "${CMAKE_SOURCE_DIR}/resources/linux/cutreel.desktop"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
        COMPONENT runtime)

    # The second is the icon that entry names, in every size the branding
    # directory has, in the hicolor theme -- the one fallback every desktop
    # reads. Named cutreel.png rather than CutReel-48.png because an Icon= line
    # names a theme entry, and a theme entry is the file's basename: the size
    # comes from the directory it is in, not from the name.
    foreach(zaro_icon_size 16 32 48 64 128 256 512)
        install(FILES "${ZARO_BRANDING_DIR}/CutReel-${zaro_icon_size}.png"
            DESTINATION
                "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${zaro_icon_size}x${zaro_icon_size}/apps"
            RENAME "cutreel.png"
            COMPONENT runtime)
    endforeach()
endif()
