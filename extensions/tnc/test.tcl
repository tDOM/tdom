
package require tnc

if {[llength $argv] != 1} {
    puts stderr "usage: $argv0 <xml-file>"
    exit 1
}

switch [file pathtype [lindex $argv 0]] {
    "relative" {
        set baseurl "file://[pwd]/[lindex $argv 0]"
    }
    default {
        set baseurl "file://[lindex $argv 0]"
    }
}

set parser [expat \
        -baseurl [tDOM::baseURL $argv]                          \
        -externalentitycommand tdom::extRefHandler              \
        -paramentityparsing notstandalone]

tnc $parser enable

$parser parsefile $argv

