# What a release archive contains, and how it is built.
#
# Included from the top-level CMakeLists after every target exists. The install
# rules themselves live next to the targets they install -- app/ and tools/ --
# because that is where the Qt deployment step has to run from; this file only
# carries the parts that are about the archive as a whole.
#
#   cmake --build   --preset release
#   cpack           --preset release
#
# produces build/release/cutreel_<version>_amd64.deb on Linux, a .dmg on
# macOS, and on Windows the installer described below.

# Windows ships one file: the bootstrapper .exe, which is an MSI inside a
# wrapper that can carry an icon. The MSI is still built -- it is what the .exe
# installs -- but it is not shipped beside it, because two downloads that
# install the same thing is a choice nobody wants to be asked to make.
#
# There is no ZIP any more either. It was the loose files for anybody who did
# not want an installer, and it was a second self-contained copy of Qt, FFmpeg
# and SDL to build, upload and keep in step for that.
#
# Everything in bin/ is self-contained there -- Qt from windeployqt, FFmpeg and
# SDL from the vcpkg DLLs installed in cmake/WindowsRuntimeDeps.cmake, and the
# MSVC runtime from InstallRequiredSystemLibraries below.
#
# Linux ships one file too: a .deb that puts the tree under /usr and adds a
# launcher entry and an icon, so that installing it gives you both a `cutreel`
# command and CutReel in the applications menu. Everything it carries is in
# lib/cutreel, found through the relative RPATH set in tools/ and app/. The
# tarball that used to sit beside it is gone; see the generator below for why.
#
# Known limitation: macOS. Only cutreel.app is made self-contained there, so
# the command-line tools in bin/ still expect a machine carrying the same
# Homebrew dependencies the disk image was built against. The same treatment
# Linux gets below would work, with @loader_path in place of $ORIGIN. Until it
# is done, those tools ride along on the image and are left behind on it when
# the app is dragged to Applications -- which is the right outcome for a binary
# that cannot start without a matching Homebrew tree anyway.

include(GNUInstallDirs)

# The Visual C++ runtime. A machine that has never had Visual Studio on it does
# not carry vcruntime140.dll, and every executable in bin/ needs it. The UCRT
# is deliberately left out: it has been part of Windows since 10, and shipping
# a copy is forty more files for a machine that already has them.
if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT runtime)
    set(CMAKE_INSTALL_UCRT_LIBRARIES OFF)
    include(InstallRequiredSystemLibraries)
endif()

install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/CutReel"
    COMPONENT runtime)

# The product and who publishes it, which are two different things and were
# spelled the same until now. The name is what somebody launches; the vendor is
# who they are installing software from -- the Publisher column in Add/Remove
# Programs, the Manufacturer in the MSI and in the Burn bundle that wraps it,
# and the Maintainer of the .deb.
#
# Not the install directory, which stays under the product's name: an installed
# path is something people have shortcuts and scripts pointing at, and moving it
# to group by publisher would strand both for no gain when there is one product.
set(CPACK_PACKAGE_NAME "CutReel")
set(CPACK_PACKAGE_VENDOR "Zaro")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "CutReel")
set(CPACK_VERBATIM_VARIABLES ON)

# One directory inside the archive rather than a bare spill of bin/ and share/
# into whatever the user unpacked it in.
set(CPACK_PACKAGE_FILE_NAME
    "CutReel-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

if(WIN32)
    set(CPACK_GENERATOR WIX)

    # --- The MSI ---------------------------------------------------------------
    # Stable for the life of the product, and the one value here that must never
    # change: WiX uses it to recognise an installed older CutReel and replace it.
    # A new GUID turns every future upgrade into a second copy installed beside
    # the first, with two Start Menu entries and no way back.
    set(CPACK_WIX_UPGRADE_GUID "0FC6F111-2EC6-47EE-955D-32DCD93A32D2")

    # One machine, one copy, in Program Files. Stated rather than left to the
    # default, which is still the "NONE" CPack 3.28 and older used: no
    # InstallScope at all, so ALLUSERS is never set and every shortcut and
    # registry key the package carries is per-user data sitting in a
    # per-machine install. WiX's validator refuses to build that -- ICE57 on
    # the desktop shortcut below, whose HKMU keypath cannot resolve to a hive
    # when nothing has said which context the install runs in -- and ICE90
    # says the same thing about the Start Menu entry CPack writes itself.
    #
    # perMachine settles it: the installer asks for elevation, DesktopFolder
    # and ProgramMenuFolder are the all-users ones, and HKMU is HKLM.
    #
    # Safe to state now and not later: an install made without an
    # InstallScope cannot be cleanly upgraded by one that has it, and no MSI
    # has ever been released -- this is the error that stopped every one of
    # them from being built.
    set(CPACK_WIX_INSTALL_SCOPE "perMachine")

    # Program Files\CutReel, one Start Menu entry, pointing at the app rather
    # than at one of the seven command-line tools beside it.
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "CutReel")
    set(CPACK_WIX_ROOT_FEATURE_TITLE "CutReel")
    set(CPACK_PACKAGE_EXECUTABLES "cutreel" "CutReel")

    # A desktop shortcut and the .cutreel file association, neither of which CPack
    # can express as a variable. See the file for what it does and why it is
    # written against the two fragment ids CPack documents as stable.
    set(CPACK_WIX_PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/wix-patch.xml")

    # The installer's own face. The product icon is what Add/Remove Programs
    # lists the entry with, and it is deliberately not the application's icon:
    # the two mean different things in a list, and the artwork says so.
    #
    # The shortcuts are absent from this list on purpose. A non-advertised
    # shortcut takes its icon from whatever it points at, so both of ours show
    # the icon compiled into cutreel.exe -- see app/CMakeLists.txt -- and
    # naming one here as well would be a second copy to keep in step.
    #
    # The two bitmaps are the WixUI stock sizes, 493x58 and 493x312. Anything
    # else is scaled to fit by the installer and looks it.
    set(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/branding/CutReel-Installer.ico")
    set(CPACK_WIX_UI_BANNER "${CMAKE_CURRENT_SOURCE_DIR}/resources/branding/installer-banner.bmp")
    set(CPACK_WIX_UI_DIALOG "${CMAKE_CURRENT_SOURCE_DIR}/resources/branding/installer-dialog.bmp")

    # WiX reads a licence as .txt or .rtf and ours is extensionless, so it is
    # copied to a name the installer's licence page will accept. COPYONLY: the
    # text of a licence is not something to run through a substitution pass.
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
                   "${CMAKE_BINARY_DIR}/License.txt" COPYONLY)
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_BINARY_DIR}/License.txt")

    # --- The .exe around the MSI -----------------------------------------------
    # An .msi cannot carry its own icon -- see cmake/bundle.wxs.in for why -- so
    # the file somebody downloads is a Burn bundle wrapping it, and that is what
    # shows the installer icon in Explorer and in a browser's download list.
    #
    # Written here rather than built here: cpack has to produce the MSI before
    # anything can wrap it, and cpack runs after the build. The workflows call
    # scripts/build-msi-bundle.ps1 on this file once it has.
    set(ZARO_BUNDLE_NAME "${CPACK_PACKAGE_NAME}")
    set(ZARO_BUNDLE_VERSION "${CPACK_PACKAGE_VERSION}")
    set(ZARO_BUNDLE_VENDOR "${CPACK_PACKAGE_VENDOR}")
    set(ZARO_BUNDLE_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/branding/CutReel-Installer.ico")
    set(ZARO_BUNDLE_LOGO "${CMAKE_CURRENT_SOURCE_DIR}/resources/branding/CutReel-Installer-64.png")
    set(ZARO_BUNDLE_LICENSE_URL
        "https://github.com/skynab/Zaro-Video/blob/dev/LICENSE")
    # The name cpack gives the MSI, spelled the same way it spells it. Absolute,
    # because light.exe resolves a relative SourceFile against its own working
    # directory and that is not necessarily this one.
    set(ZARO_BUNDLE_MSI "${CMAKE_BINARY_DIR}/${CPACK_PACKAGE_FILE_NAME}.msi")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundle.wxs.in"
                   "${CMAKE_BINARY_DIR}/bundle.wxs" @ONLY)
elseif(APPLE)
    # macOS ships one file: a .dmg holding cutreel.app beside a symlink to
    # /Applications, which is the install gesture every Mac user already knows.
    # It replaces a .tar.gz, and the reason is not presentation. A tarball is
    # opened by Archive Utility, which copies the quarantine flag off the
    # download onto every file it unpacks -- and CutReel is signed by
    # macdeployqt with an ad-hoc signature, which is not a Developer ID and
    # carries no notarisation ticket. Gatekeeper rejects that combination with
    # "cutreel is damaged and can't be opened", which is a lie about the
    # bundle and reads like a corrupt download.
    #
    # The disk image does not fix that, and nothing here can: a file copied out
    # of a quarantined .dmg inherits the flag exactly as one unpacked from a
    # quarantined tarball does, so the first launch is blocked either way. Only
    # a Developer ID signature and notarisation remove the dialog. What the
    # image buys is a container macOS understands -- one mount, one drag, no
    # Archive Utility -- and one place to say so; see the README for the
    # xattr command that clears the flag until CutReel is notarised.
    set(CPACK_GENERATOR DragNDrop)

    # What Finder titles the mounted volume, and what the eject entry in the
    # sidebar is called. CPACK_PACKAGE_FILE_NAME is the default and it is the
    # file name -- CutReel-0.7.0-Darwin-arm64 -- which is right for a download
    # and wrong for a window title.
    set(CPACK_DMG_VOLUME_NAME "CutReel ${PROJECT_VERSION}")

    # zlib-compressed and read-only, the format every application .dmg uses.
    set(CPACK_DMG_FORMAT "UDZO")

    # No click-through licence on mount. CPACK_RESOURCE_FILE_LICENSE is set
    # near the top of this file, and older CPack turned any non-default
    # value into a software licence agreement the user has to Agree to before
    # Finder will open the volume -- built with `hdiutil udifrez`, which macOS
    # 12 deprecated. CMP0133 already defaults this off for a project requiring
    # CMake 3.24, but it is the difference between an image that builds and one
    # that does not, so it is stated rather than inherited.
    set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE OFF)

    # The /Applications symlink is CPack's default and is deliberately left on;
    # without it the window has nothing to drag the app onto.
else()
    # One file, and it is the .deb. There was a tarball beside it -- unpack it
    # anywhere, run bin/cutreel -- and it was a second Linux package to
    # build, upload, test and answer questions about, for the same tree the .deb
    # already installs. The .deb is the one that puts CutReel in the
    # applications menu, puts `cutreel` on PATH, and can be removed again with
    # `apt remove`; the tarball could do none of those, so shipping both meant
    # offering a download that is worse in every way except that it needs no
    # root, and then answering for it.
    #
    # Nothing about the tarball's contents is gone: the install rules are the
    # same ones, and `cmake --install build/release --prefix somewhere` still
    # produces exactly the tree it carried, for anybody who wants it without a
    # package manager.
    set(CPACK_GENERATOR DEB)

    # --- The .deb --------------------------------------------------------------
    # Lowercase, because a Debian package name has to be. The rest of CPack's
    # naming is left to the generator, which spells it cutreel_0.7.0_amd64.deb.
    set(CPACK_DEBIAN_PACKAGE_NAME "cutreel")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION "video")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR}")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/skynab/Zaro-Video")

    # Four, and only four. The package carries its own Qt, FFmpeg and SDL, so
    # the only things it needs from the distribution are the ones no program can
    # bring with it. Naming more would be naming them wrongly: what the desktop
    # stack is called moves between releases -- libasound2 became libasound2t64
    # in 24.04 -- and a Depends line that names a package apt cannot find is an
    # install that fails on a machine that had everything it needed.
    #
    # hicolor-icon-theme is the exception, and it earns it twice over. It owns
    # /usr/share/icons/hicolor and the index.theme in it, which is what makes
    # the directories this package drops cutreel.png into a theme a desktop
    # will look in rather than three loose folders; without it the launcher
    # entry installs correctly and shows a blank icon. And it is safe to name
    # in a way the libraries above are not: an architecture-independent data
    # package, not an ABI, so it has been spelled the same in every Debian and
    # Ubuntu release rather than being renamed by a toolchain transition.
    #
    # Deliberately not CPACK_DEBIAN_PACKAGE_SHLIBDEPS: dpkg-shlibdeps resolves
    # every library a binary links, ours included, and cannot name a package for
    # the copy of Qt in lib/cutreel because no package provides it.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libc6, libstdc++6, libgcc-s1, hicolor-icon-theme")
endif()

# --- What the Linux packages carry ---------------------------------------------
if(UNIX AND NOT APPLE)
    # Everything the installed executables and plugins link, resolved the way
    # the loader resolves it, minus the list below.
    #
    # The list is the libraries that belong to the machine rather than to us:
    # the C and C++ runtimes, the graphics and windowing stack, the sound
    # servers, and the handful of system services Qt talks to. Bundling any of
    # them is how a program stops working on the next machine -- a copied libGL
    # cannot talk to a driver it was not built beside, and a copied libz that
    # loads before the system's changes what every other library in the process
    # sees. Everything else -- Qt, FFmpeg, SDL, and the codec libraries FFmpeg
    # pulls in -- is ours to ship, because no distribution promises the version
    # we built against.
    #
    # Matched on the file name, so it applies whether a library was found in
    # /lib, /usr/lib or the Qt the build used.
    set(zaro_system_libraries
        "ld-linux.*" "libc\\.so.*" "libm\\.so.*" "libdl\\.so.*" "libpthread\\.so.*"
        "librt\\.so.*" "libresolv\\.so.*" "libutil\\.so.*"
        "libgcc_s\\.so.*" "libstdc\\+\\+\\.so.*"
        # Graphics: the driver's, never ours.
        "libGL.*\\.so.*" "libEGL.*\\.so.*" "libOpenGL\\.so.*" "libGLdispatch\\.so.*"
        "libGLU\\.so.*" "libdrm\\.so.*" "libgbm\\.so.*" "libvulkan\\.so.*"
        # Windowing.
        "libX11.*\\.so.*" "libXext\\.so.*" "libXrender\\.so.*" "libXi\\.so.*"
        "libXrandr\\.so.*" "libXcursor\\.so.*" "libXfixes\\.so.*" "libXau\\.so.*"
        "libXdmcp\\.so.*" "libxcb.*\\.so.*" "libwayland.*\\.so.*" "libxkbcommon.*\\.so.*"
        # Fonts: fontconfig reads the machine's configuration and freetype is
        # built against it, so the pair has to come from the same machine.
        "libfontconfig\\.so.*" "libfreetype\\.so.*"
        # Sound.
        "libasound\\.so.*" "libpulse.*\\.so.*" "libjack.*\\.so.*" "libpipewire.*\\.so.*"
        # System services and the libraries everything already has.
        "libglib-2\\.0\\.so.*" "libgobject-2\\.0\\.so.*" "libgio-2\\.0\\.so.*"
        "libgmodule-2\\.0\\.so.*" "libgthread-2\\.0\\.so.*"
        "libdbus-1\\.so.*" "libsystemd\\.so.*" "libudev\\.so.*" "libselinux\\.so.*"
        "libz\\.so.*" "libcrypto\\.so.*" "libssl\\.so.*")

    install(RUNTIME_DEPENDENCY_SET zaro_runtime_deps
        PRE_EXCLUDE_REGEXES  ${zaro_system_libraries}
        POST_EXCLUDE_REGEXES ${zaro_system_libraries}
        DESTINATION "${ZARO_PRIVATE_LIBDIR}"
        COMPONENT runtime)

    # The same list, one regex per line, for the CI check that proves the
    # package is self-contained: every library the loader resolves for an
    # installed binary has to come from lib/cutreel or be on this list. Written
    # from here rather than copied into the workflow so that there is one list,
    # and adding a library to it above is adding it there as well.
    list(JOIN zaro_system_libraries "\n" zaro_system_libraries_text)
    file(WRITE "${CMAKE_BINARY_DIR}/system-libraries.txt"
        "${zaro_system_libraries_text}\n")

    # Copying the libraries into lib/cutreel is half of the job. The other half
    # is making sure each of them can find the others from there, which the
    # executables' RPATH does not do for them: a bundled libavcodec looking for
    # its libvpx is not consulting cutreel's RPATH to do it. The script runs
    # after the copy above, and gives every object in the directory an RPATH of
    # $ORIGIN. See the script for the loader rule that makes this necessary,
    # and for the release that shipped without it.
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstallBundledRpath.cmake.in"
                   "${CMAKE_BINARY_DIR}/InstallBundledRpath.cmake" @ONLY)
    install(SCRIPT "${CMAKE_BINARY_DIR}/InstallBundledRpath.cmake" COMPONENT runtime)
endif()

# Nothing in the archive should be a test binary or a header.
set(CPACK_COMPONENTS_ALL runtime)
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)

# There is one component and it is the product, so the installer's feature tree
# says "CutReel" and offers no choice, rather than presenting a checkbox called
# "runtime" that nothing works without.
set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "CutReel")
set(CPACK_COMPONENT_RUNTIME_REQUIRED ON)

# Read once per generator; see the file for what has to differ between them.
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPackOptions.cmake")

include(CPack)
