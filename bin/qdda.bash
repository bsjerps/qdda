#============================================================================
# Title       : qdda.bash
# Description : bash_completion file for qdda tool
# Author      : Bart Sjerps <bart@outrun.nl>
# License     : GPLv3+
# ---------------------------------------------------------------------------
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License at <http://www.gnu.org/licenses/> for
# more details.
# ---------------------------------------------------------------------------
# Revision history:
# 2017-11-09 : Created
# ---------------------------------------------------------------------------
_qdda() {
  lsdevs() {
    find /dev/mapper -type l 2>/dev/null
    find /dev -maxdepth 1 -type b -name "sd*" -o -name "emcpower*" -o -name "scini*"
  }
  COMPREPLY=()
  local cur="${COMP_WORDS[COMP_CWORD]}"
  local prev="${COMP_WORDS[COMP_CWORD-1]}"
  local cmd="${COMP_WORDS[1]}"
  local opts="-B -T -a -b -c -d -f -i -n -q -r -t -V -x -h -H"
  local tstsz="0 2 16 64 256 1024"
  local blksz="8 16 128"
  local compr="x1 x2 v1"
  local tmpd="/tmp $TMP $TMPDIR"
  case ${prev} in
    -h|-H|-V) return 0 ;;
    -f|-i) COMPREPLY=($(compgen -o plusdirs -f -X '!*.db' -- "$cur")) ; return 0 ;;
    -t)    COMPREPLY=($(compgen -W "${tstsz}" -- ${cur})) ; return 0 ;;
    -B)    COMPREPLY=($(compgen -W "${blksz}" -- ${cur})) ; return 0 ;;
    -b)    COMPREPLY=($(compgen -W "0"        -- ${cur})) ; return 0 ;;
    -c)    COMPREPLY=($(compgen -W "${compr}" -- ${cur})) ; return 0 ;;
    -T)    COMPREPLY=($(compgen -W "${tmpd}"  -- ${cur})) ; return 0 ;;
  esac
  case ${cur} in
    -*) COMPREPLY=($(compgen -W "${opts}" -- ${cur})) ; return 0 ;;
  esac
  COMPREPLY=($(compgen -W "$(lsdevs)" -- ${cur}))
  unset -f lsdevs
  return 0
}
complete -o filenames -F _qdda qdda
