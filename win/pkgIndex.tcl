# tDOM Tcl package index file

package ifneeded tdom 0.8.1 \
    "[list load   [file join $dir libtdom081[info sharedlibextension] ] tdom];\
     [list source [file join $dir tdom.tcl]]"
