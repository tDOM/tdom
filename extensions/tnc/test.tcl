
package require tnc

if {[llength $argv] != 1} {
    puts stderr "usage: $argv0 <xml-file>"
    exit 1
}

set parser [expat \
        -baseurl [tDOM::baseURL $argv]                          \
        -externalentitycommand tdom::extRefHandler              \
        -paramentityparsing notstandalone]

tnc $parser enable

$parser parsefile $argv

