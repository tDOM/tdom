# loadtdom.tcl --
#
# This file is [source]d by all.tcl and all test files, to ensure, that
# the tcltest package and the lastest tdom build is present.


if {[lsearch [namespace children] ::tcltest] == -1} {
    package require tcltest
    namespace import ::tcltest::*
}

if {[lsearch [namespace children] ::tdom] == -1} {
    set tdomVersion 0.7.5 
    set bin "libtdom[set tdomVersion][info sharedlibextension]"
    
    source [file join [file dir [info script]] ../lib tdom.tcl]
    if {[catch {[load [file join [file dir [info script]] ../unix $bin]]}]} {
        if {[catch {[load [file join [file dir [info script]] ../win $bin]]}]} {
            # Just propagate the error, therefor no catch
            package require tdom $tdomVersion
        }
    }
}


