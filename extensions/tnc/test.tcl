load [file dirname [info script]]/../../unix/tdom0.6.so
load [file dirname [info script]]/tnc0.1.so


#Yes, yes, this should be use tcllib's uri module or something
#similar..
proc externalEntityRefHandler {base systemId publicId} {
    if {[regexp {^[a-zA-Z]+:/} $systemId]}  {
        # Seems to be not relative to the base
        if {[regexp { *file:(.*)} $systemId dummy path]} {
            return [list filename $systemId $path]
        } else {
            return -code error  -errorinfo "externalEntityRefHandler: can only handle file URL's"
        }
    } else {
        if {[regexp { *file:(.*)} $base dummy basepath]} {
            set basedir [file dirname $basepath]
            set entitypath "${basedir}/${systemId}"
            return [list filename "file://${basedir}/${systemId}" $entitypath]
        } else {
            return -code error  -errorinfo "externalEntityRefHandler: can only handle file URL's"
        }
    }
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
        -baseurl $baseurl                                       \
        -externalentitycommand externalEntityRefHandler         \
        -paramentityparsing notstandalone]

tnc $parser enable

$parser parsefile [lindex $argv 0]

