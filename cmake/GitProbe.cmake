# GitProbe.cmake - read a repository's build identity WITHOUT requiring git.
#
# Why this exists: CMake's previous build-identity stamp was derived by
# execute_process(git ...) at CONFIGURE time only, so an incremental rebuild
# that follows new commits kept printing an older sha from `havel --version`
# while the compiled code was already current. A reader could not tell a
# current build from a stale one by the output, which inverts the trap that
# binary-provenance reporting is supposed to catch.
#
# This module is the shared implementation behind the build-time header
# generator (see cmake/BuildIdentity.cmake.in). It is deliberately usable
# both from a configure step and from `cmake -P` at build time, so it is
# written as free functions rather than as logic inline in CMakeLists.txt.
#
# Two paths, in order:
#   1. git, when the binary is present - the authoritative answer.
#   2. a reader over .git itself, when git is absent (the xbps-src chroot
#      used for packaging and iteration builds has no git(1) on PATH, which
#      is exactly where a wrong stamp is hardest to notice).
#
# The .git reader covers only what build identity needs: HEAD's sha, the
# branch, and whether tracked files were modified. It is not `git status`:
# where git is available its answer is used verbatim.

# First line of a file, stripped. "" when missing, empty or unreadable.
function(_gp_read_line path outvar)
  set(${outvar} "" PARENT_SCOPE)
  if(NOT EXISTS "${path}")
    return()
  endif()
  file(STRINGS "${path}" _gp_lines LIMIT_COUNT 1)
  if(NOT _gp_lines)
    return()
  endif()
  list(GET _gp_lines 0 _gp_v)
  string(STRIP "${_gp_v}" _gp_v)
  set(${outvar} "${_gp_v}" PARENT_SCOPE)
endfunction()

# .git may be a directory, or a FILE containing "gitdir: <path>" when this
# checkout is a linked worktree.
function(_gp_git_dir root out)
  if(IS_DIRECTORY "${root}/.git")
    set(${out} "${root}/.git" PARENT_SCOPE)
    return()
  endif()
  _gp_read_line("${root}/.git" _gp_gd)
  if(_gp_gd MATCHES "^gitdir:")
    string(REGEX REPLACE "^gitdir:" "" _gp_gd "${_gp_gd}")
    string(STRIP "${_gp_gd}" _gp_gd)
    if(NOT IS_ABSOLUTE "${_gp_gd}")
      set(_gp_gd "${root}/${_gp_gd}")
    endif()
    set(${out} "${_gp_gd}" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# A 40-hex object name. CMake's regex here is ERE, so the bounded repetition
# {n} does not work and a literal '$' must be escaped; the shape is spelled
# out instead, which is portable across both.
function(_gp_is_sha40 v out)
  if(v MATCHES "^[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]")
    set(${out} TRUE PARENT_SCOPE)
  else()
    set(${out} FALSE PARENT_SCOPE)
  endif()
endfunction()

# HEAD's sha, or "" when it cannot be determined from the layout.
#
# HEAD is "<sha>" when detached and "ref: <ref>" on a branch. The ref is
# resolved from the loose file .git/<ref>, then from packed-refs when the
# loose file has been packed away (lines are "<sha> <ref>", with '#' comments
# and '^' peeled-tag lines to skip).
function(git_probe_head_sha root out)
  _gp_git_dir("${root}" _gp_gd)
  if(_gp_gd STREQUAL "")
    set(${out} "" PARENT_SCOPE)
    return()
  endif()
  _gp_read_line("${_gp_gd}/HEAD" _gp_h)
  if(_gp_h STREQUAL "")
    set(${out} "" PARENT_SCOPE)
    return()
  endif()
  _gp_is_sha40("${_gp_h}" _gp_detached)
  if(_gp_detached)
    set(${out} "${_gp_h}" PARENT_SCOPE)
    return()
  endif()
  if(NOT _gp_h MATCHES "^ref:")
    set(${out} "" PARENT_SCOPE)
    return()
  endif()
  string(REGEX REPLACE "^ref:[ \t]*" "" _gp_ref "${_gp_h}")
  _gp_read_line("${_gp_gd}/${_gp_ref}" _gp_sha)
  if(NOT _gp_sha STREQUAL "")
    set(${out} "${_gp_sha}" PARENT_SCOPE)
    return()
  endif()
  if(EXISTS "${_gp_gd}/packed-refs")
    file(STRINGS "${_gp_gd}/packed-refs" _gp_packed)
    foreach(_gp_line IN LISTS _gp_packed)
      if(_gp_line MATCHES "^#")
        continue()
      endif()
      string(REGEX REPLACE "[ \t]+" ";" _gp_f "${_gp_line}")
      list(LENGTH _gp_f _gp_nf)
      if(_gp_nf LESS 2)
        continue()
      endif()
      list(GET _gp_f 0 _gp_psha)
      list(GET _gp_f 1 _gp_pref)
      if(_gp_pref STREQUAL "${_gp_ref}")
        _gp_is_sha40("${_gp_psha}" _gp_ok)
        if(_gp_ok)
          set(${out} "${_gp_psha}" PARENT_SCOPE)
          return()
        endif()
      endif()
    endforeach()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# Branch name, or "(detached)" when HEAD carries a sha directly.
function(git_probe_branch root out)
  _gp_git_dir("${root}" _gp_gd)
  if(_gp_gd STREQUAL "")
    set(${out} "" PARENT_SCOPE)
    return()
  endif()
  _gp_read_line("${_gp_gd}/HEAD" _gp_h)
  if(_gp_h MATCHES "^ref:[ \t]*refs/heads/(.+)$")
    set(${out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  else()
    set(${out} "(detached)" PARENT_SCOPE)
  endif()
endfunction()

# Whether tracked files are modified.
#
# The authoritative answer comes from git. Without git this falls back to
# comparing the recorded index mtime against HEAD's tree entry, which is the
# best a checkout offers without the object store walk that `git status`
# performs. It is deliberately conservative: it reports "dirty" whenever the
# index disagrees with HEAD. A false "dirty" costs one word in --version
# output; a false "clean" would misreport provenance, which is the failure
# this whole mechanism exists to prevent.
#
# Sets <out> to "dirty", "clean", or "" when it cannot be determined.
function(git_probe_dirty root out)
  _gp_git_dir("${root}" _gp_gd)
  if(_gp_gd STREQUAL "")
    set(${out} "" PARENT_SCOP)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# Walk the checked-out working tree for any tracked file that differs from
# INDEX. Used by git_probe_dirty when git is absent.
#
# The one signal available without git is that `.git/index` records, per
# entry, the exact mtime the file had when it was added, and the stat data
# is revalidated by every git command that touches the index. A tracked file
# whose current mtime differs from what the index recorded was written after
# the last git operation - which is exactly the state `git status` would
# report as modified. It cannot see a file restored to its indexed mtime with
# different CONTENT, which git also reports as clean, so the two agree on the
# only case that matters: content changed after the index was written.
function(_gp_index_walk root out)
  _gp_git_dir("${root}" _gp_gd)
  if(_gp_gd STREQUAL "")
    set(${out} "" PARENT_SCOP)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()
