
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

set catalogfile ""
set skip [list]
set match [list]
set matchgroup [list]
set matchfile [list]

proc putsUsage {{channel stderr}} {
    puts $channel "usage: $argv0 ?options? path/to/catalog.xml"
}

proc processArgs {argc argv} {
    global catalogfile
    global skip
    global match
    global matchgroup
    global matchfile

    if {$argc == 0 || $argc % 2 == 0} {
        putsUsage
        exit 1
    }
    
    foreach {option value} $argv {
        if {$value eq ""} {
            break
        }
        switch $option {
            "-match" {
                set match $value
            }
            "-matchgroup" {
                set matchgroup $value
            }
            "-matchfile" {
                set matchfile $value
            }
            "-skip" {
                set skip $value
            }
            default {
                puts stderr "Unknown option \"$option\""
                putsUsage
                exit 1
            }
        }
    }
    set catalogfile [lindex $argv end]
}

set compareOK 0
set compareDIFF 0

# This is the callback proc for xslt:message elements. This proc is
# called once every time an xslt:message element is encountered during
# processing the stylesheet. The callback proc simply sends the text
# message to stderr.
proc xsltmsgcmd {msg terminate} {
    puts stderr "xslt message: '$msg'"
}

proc readCatalog {catalogPath} {
    global catalogDir
    global infoset

    set fd [open $catalogPath]
    set doc [dom parse -channel $fd]
    close $fd
    set catalogDir [file dirname $catalogPath]
    set infosetxsl [file join $catalogDir .. TOOLS infoset.xsl]
    set infosetdoc [dom parse -keepEmpties [xmlReadFile $infosetxsl]]
    set infoset [$infosetdoc toXSLTcmd]
    return $doc
}

proc checkAgainstPattern {patternlist text} {
    foreach pattern $patternlist {
        if {[string match $pattern $text]} {
            return 1
        }
    }
    return 0
}

proc runFilepath {filepath} {
    global matchgroup

    if {![llength $matchgroup]} {
        return 1
    }
    return [checkAgainstPattern $matchgroup $filepath]
}

proc runXslfile {xslfile} {
    global matchfile

    if {![llength $matchfile]} {
        return 1
    }
    return [checkAgainstPattern $matchfile $xslfile]
}

proc runPurpose {purpose} {
    global match

    if {![llength $match]} {
        return 1
    }
    return [checkAgainstPattern $match $purpose]
}

proc skip {purpose} {
    global skip

    return [checkAgainstPattern $skip $purpose]
}

proc runTest {testcase} {
    global catalogDir
    global majorpath
    global infoset
    global compareOK
    global compareDIFF

    set filepath [$testcase selectNodes string(file-path)]
    if {![runFilepath $filepath]} {
        return
    }
    set scenario [$testcase selectNodes scenario]
    if {[llength $scenario] != 1 || [$scenario @operation] ne "standard"} {
        puts "Non-standard scenario!"
        puts [$testcase asXML]
        return
    }
    set xmlfile [$scenario selectNodes \
                     {string(input-file[@role="principal-data"])}]
    set xmlfile [file join $catalogDir $majorpath $filepath $xmlfile]
    set xslfile [$scenario selectNodes \
                     {string(input-file[@role="principal-stylesheet"])}]
    if {![runXslfile $xslfile]} {
        return
    }
    set xslfile [file join $catalogDir $majorpath $filepath $xslfile]
    set xmlout [$scenario selectNodes \
                    {string(output-file[@role="principal" and @compare="XML"])}]
    set xmloutfile ""
    if {$xmlout ne ""} {
        set xmloutfile [file join $catalogDir $majorpath "REF_OUT" $filepath \
                            $xmlout]
    }
    set purpose [$testcase selectNodes string(purpose)]
    if {![runPurpose $purpose]} {
        return
    }
    if {[skip $purpose]} {
        puts "Skipping $filepath: $purpose"
        return
    }
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
    set resultDoc ""
    if {[catch {$xmldoc xslt -xsltmessagecmd xsltmsgcmd $xsltdoc resultDoc} \
             errMsg]} {
        puts stderr $errMsg
    }
    if {$xmloutfile ne "" && [llength [info commands $resultDoc]]} {
        if {![catch {
            set refdoc [dom parse -keepEmpties [xmlReadFile $xmloutfile]]
        } errMsg]} {
            set refinfosetdoc [$infoset $refdoc]
            set resultinfosetdoc [$infoset $resultDoc]
            if {[$refinfosetdoc asXML] ne [$resultinfosetdoc asXML]} {
                incr compareDIFF
                puts "Result and ref differ."
                puts "Ref:"
                puts [$refinfosetdoc asXML]
                puts "Result:"
                puts [$resultinfosetdoc asXML]
            } else {
                incr compareOK
            }
            $refinfosetdoc delete
            $resultinfosetdoc delete
        } else {
            puts "Unable to parse REF doc. Reason:\n$errMsg"
        }
    }
    $xmldoc delete
    $xsltdoc delete
    catch {$resultDoc delete}
}

proc runTests {catalogRoot} {
    global majorpath
    global compareOK
    global compareDIFF

    foreach testcatalog [$catalogRoot selectNodes test-catalog] {
        if {[$testcatalog @submitter] ne "Lotus"} {
            continue
        }
        set majorpath [$testcatalog selectNodes string(major-path)]
        foreach testcase [$testcatalog selectNodes test-case] {
            runTest $testcase
        }
        puts "Finished."
        puts "Compare OK: $compareOK"
        puts "Compare FAIL: $compareDIFF"
    }
}

processArgs $argc $argv
set catalogDoc [readCatalog $catalogfile]
runTests [$catalogDoc documentElement]

proc exit args {}
