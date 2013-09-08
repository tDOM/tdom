
# Helper script to run xslt 1.0 conformance test suite created by the
# OASIS XSLT / XPath Conformance Technical Committee. 

package require tdom

# The following is not needed, given, that tDOM is correctly
# installed. This code only ensures, that the tDOM script library gets
# sourced, if the script is called with a tcldomsh out of the build
# dir of a complete tDOM source installation.
if {[lsearch [namespace children] ::tDOM] == -1} {
    # tcldomsh without the script library. Source the lib.
    source [file join [file dir [info script]] ../lib tdom.tcl]
}

# Import the tDOM helper procs
namespace import tDOM::*

if {$argc != 1} {
    puts "usage: $argv0 path/to/catalog.xml"
    exit 1
}

set skiplist {
    "Show that apply-imports really means imports, not includes"
}
set skiplist [list]
set match [list]
array set skiparray [list]
foreach skip $skiplist {
    set skiparray($skip) 1
}

# This is the callback proc for xslt:message elements. This proc is
# called once every time an xslt:message element is encountered during
# processing the stylesheet. The callback proc simply sends the text
# message to stderr.
proc xsltmsgcmd {msg terminate} {
    puts stderr "xslt message: '$msg'"
}

proc readCatalog {catalogPath} {
    global catalogDir

    set fd [open $catalogPath]
    set doc [dom parse -channel $fd]
    close $fd
    set catalogDir [file dirname $catalogPath]
    return $doc
}

proc runTest {testcase} {
    global catalogDir
    global majorpath
    global skiparray
    global match

    set filepath [$testcase selectNodes string(file-path)]
    set scenario [$testcase selectNodes scenario]
    if {[llength $scenario] != 1 || [$scenario @operation] ne "standard"} {
        puts "Non-standard scenario!"
        puts [$testcase asXML]
        return
    }
    set principaldata [$scenario selectNodes {input-file[@role="principal-data"]}]
    if {[llength $principaldata] != 1} {
        puts "Non-standard scenario - not exact one xml input file!"
        puts [$testcase asXML]
        return
    }
    set xmlfile [file join $catalogDir $majorpath $filepath [$principaldata text]]
    set principalstylesheet [$scenario selectNodes {input-file[@role="principal-stylesheet"]}]
    if {[llength $principalstylesheet] != 1} {
        puts "Non-standard scenario - not exact one xsl input file!"
        puts [$testcase asXML]
        return
    }
    set purpose [$testcase selectNodes string(purpose)]
    set matches 0
    if {[llength $match]} {
        foreach pattern $match {
            if {[string match $pattern $purpose]} {
                set matches 1
                break
            }
        }
        if {!$matches} {
            return
        }
    }
    if {[info exists skiparray($purpose)]} {
        puts "Skipping $filepath: $purpose"
        return
    }
    set xslfile [file join $catalogDir $majorpath $filepath [$principalstylesheet text]]
    set xmldoc [dom parse -baseurl [baseURL $xmlfile] \
                    -externalentitycommand extRefHandler \
                    -keepEmpties \
                    [xmlReadFile $xmlfile] ]
    dom setStoreLineColumn 1
    set xsltdoc [dom parse -baseurl [baseURL $xslfile] \
                       -externalentitycommand extRefHandler \
                       -keepEmpties \
                       [xmlReadFile $xslfile] ]
    dom setStoreLineColumn 0
    if {[catch {$xmldoc xslt -xsltmessagecmd xsltmsgcmd $xsltdoc resultDoc} \
             errMsg]} {
        puts stderr $errMsg
    }
    $xmldoc delete
    $xsltdoc delete
    catch {$resultDoc delete}
}

proc runTests {catalogRoot} {
    global majorpath

    foreach testcatalog [$catalogRoot selectNodes test-catalog] {
        if {[$testcatalog @submitter] ne "Lotus"} {
            continue
        }
        set majorpath [$testcatalog selectNodes string(major-path)]
        foreach testcase [$testcatalog selectNodes test-case] {
            runTest $testcase
        }
    }
}

set catalogDoc [readCatalog $argv]
runTests [$catalogDoc documentElement]
