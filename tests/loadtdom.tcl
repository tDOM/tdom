# loadtdom.tcl --
#
# This file is [source]d by all.tcl and all test files, to ensure, that
# the tcltest package and the lastest tdom build is present.
#
# RCS: @(#) $Id$
#

if {[lsearch [namespace children] ::tcltest] == -1} {
    package require tcltest
    namespace import ::tcltest::*
}
if {[lsearch [namespace children] ::tdom] == -1} {

    package require tdom
}

