# Per-generator settings, read once for each generator in CPACK_GENERATOR.
#
# CPack includes this file with CPACK_GENERATOR set to the single generator it
# is about to run, which is the only place a setting can differ between the two
# packages one `cpack` invocation produces. Everything that is the same for both
# belongs in Packaging.cmake instead.

# Where the files sit inside the package.
#
# A .deb is a filesystem image: its paths are the paths on the machine, so the
# tree has to be rooted at /usr for bin/ to become /usr/bin. An archive is not,
# and rooting one at /usr would hand somebody a tarball that unpacks to a `usr`
# directory they have to reach through to find anything. A .dmg is the same
# case as an archive: what the prefix names is the root of the mounted volume,
# and a `usr` folder there is a window with nothing in it worth double-clicking
# and cutreel.app buried three levels down.
#
# All three are stated rather than left to the generator's default: which
# default applies to which generator is exactly the kind of thing that is true
# until it is not, and the relative RPATH in every executable depends on this
# being bin/../lib/cutreel in every package.
if(CPACK_GENERATOR STREQUAL "DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
elseif(CPACK_GENERATOR STREQUAL "TGZ" OR CPACK_GENERATOR STREQUAL "ZIP"
       OR CPACK_GENERATOR STREQUAL "DragNDrop")
    set(CPACK_PACKAGING_INSTALL_PREFIX "")
endif()
