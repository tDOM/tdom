
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
set loglevel 0
set skip [list]
set match [list]
set matchgroup [list]
set matchfile [list]
set matchcatalog [list]

proc putsUsage {{channel stderr}} {
    puts $channel "usage: $argv0 ?options? path/to/catalog.xml"
    puts $channel "where options can be:"
    puts $channel "-loglevel <int>"
    puts $channel "-skip patternlist"
    puts $channel "-match patternlist"
    puts $channel "-matchgroup patternlist"
    puts $channel "-matchfile patternlist"
    puts $channel "-matchcatalog patternlist"
}

proc processArgs {argc argv} {
    global catalogfile
    global skip
    global match
    global matchgroup
    global matchfile
    global matchcatalog
    global loglevel

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
            "-matchcatalog" {
                set matchcatalog $value
            }
            "-loglevel" {
                if {![string is interger -strict $value]} {
                    set loglevel $value
                } else {
                    putsUsage
                    exit 1
                }
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

proc matchcatalog {testcatalog} {
    global matchcatalog

    if {![llength $matchcatalog]} {
        return 1
    }
    return [checkAgainstPattern $matchcatalog $testcatalog]
}

proc log {level text {detail ""}} {
    global loglevel

    if {$level <= $loglevel} {
        puts $text
    }
    if {$detail ne "" && $level < $loglevel} {
        puts $detail
    }
}

proc findFile {filename path} {
    # The Microsoft testcatalog includes tests for which the physical
    # file name differ in case from the file name given by the test
    # definition. This proc tries to identify the correct file name in
    # such a case.

    log 3 "findFile called with $filename $path"
    set filelist [glob -nocomplain -tails -directory $path *]
    set nocasequal [lsearch -exact -nocase $filelist $filename]
    if {[llength $nocasequal] == 1} {
        if {$nocasequal >= 0} {
            return [file join $path [lindex $filelist $nocasequal]]
        }
    }
    return ""
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
    if {[llength $scenario] != 1 } {
        log 0 "Non-standard scenario!"
        log 0 [$testcase asXML]
        return
    }
    set operation [$scenario @operation]
    switch $operation {
        "standard" -
        "execution-error" {}
        default {
            log 0 "Non-standard scenario!"
            log 0 [$testcase asXML]
            return
        }
    }
    set xmlfile [$scenario selectNodes \
                     {string(input-file[@role="principal-data"])}]
    set xmlfile [file join $catalogDir $majorpath $filepath $xmlfile]
    if {![file readable $xmlfile]} {
        set xmlfile [findFile $xmlfile \
                         [file join $catalogDir $majorpath $filepath]]
        if {$xmlfile eq ""} {
            log 0 "Couldn't find xmlfile \
                  [$scenario selectNodes \
                     {string(input-file[@role="principal-data"])}]"
            return
        }
    }
    set xslfile [$scenario selectNodes \
                     {string(input-file[@role="principal-stylesheet"])}]
    if {![runXslfile $xslfile]} {
        return
    }
    if {![file readable $xslfile]} {
        set xslfile [findFile $xslfile \
                         [file join $catalogDir $majorpath $filepath]]
        if {$xslfile eq ""} {
            log 0 "Couldn't find xslfile \
                  [$scenario selectNodes \
                     {string(input-file[@role="principal-stylesheet"])}]"
            return
        }
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
        log 1 "Skipping $filepath: $purpose"
        return
    }
    if {[catch {
        set xmldoc [dom parse -baseurl [baseURL $xmlfile] \
                        -externalentitycommand extRefHandler \
                        -keepEmpties \
                        [xmlReadFile $xmlfile] ]
    } errMsg]} {
        log 0 "Unable to parse xml file '$xmlfile'. Reason:\n$errMsg"
        return
    }
    dom setStoreLineColumn 1
    if {[catch {
        set xsltdoc [dom parse -baseurl [baseURL $xslfile] \
                         -externalentitycommand extRefHandler \
                         -keepEmpties \
                         [xmlReadFile $xslfile] ]
    } errMsg]} {
        dom setStoreLineColumn 0
        log 0 "Unable to parse xsl file '$xslfile'. Reason:\n$errMsg"
        return
    }
    dom setStoreLineColumn 0
    set resultDoc ""
    if {[catch {$xmldoc xslt -xsltmessagecmd xsltmsgcmd $xsltdoc resultDoc} \
             errMsg]} {
        if {$operation ne "execution-error"} {
            log 0 $errMsg
        }
    } else {
        if {$operation eq "execution-error"} {
            log 0 "$xslfile - test should have failed, but didn't."
        }
    }
    if {$xmloutfile ne "" && [llength [info commands $resultDoc]]} {
        if {![catch {
            set refdoc [dom parse -keepEmpties [xmlReadFile $xmloutfile]]
        } errMsg]} {
            set refinfosetdoc [$infoset $refdoc]
            set resultinfosetdoc [$infoset $resultDoc]
            if {[$refinfosetdoc asXML -indent none] 
                ne [$resultinfosetdoc asXML -indent none]} {
                incr compareDIFF
                log 1 "Result and ref differ."
                log 2 "Ref:"
                log 2 [$refinfosetdoc asXML]
                log 2 "Result:"
                log 2 [$resultinfosetdoc asXML]
            } else {
                incr compareOK
            }
            $refinfosetdoc delete
            $resultinfosetdoc delete
        } else {
            log 3 "Unable to parse REF doc. Reason:\n$errMsg"
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
        if {![matchcatalog [$testcatalog @submitter]]} {
            continue
        }
        set majorpath [$testcatalog selectNodes string(major-path)]
        foreach testcase [$testcatalog selectNodes test-case] {
            runTest $testcase
        }
    }
    log 0 "Finished."
    log 0 "Compare OK: $compareOK"
    log 0 "Compare FAIL: $compareDIFF"
}

processArgs $argc $argv
set catalogDoc [readCatalog $catalogfile]
runTests [$catalogDoc documentElement]

proc exit args {}
