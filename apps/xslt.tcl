#----------------------------------------------------------------------------
#   Copyright (c) 1999-2001 Jochen Loewer (loewerj@hotmail.com)   
#----------------------------------------------------------------------------
#
#   $Header$
#
#
#   A simple command line XSLT processor using tDOM XSLT engine.
#
#
#   The contents of this file are subject to the Mozilla Public License
#   Version 1.1 (the "License"); you may not use this file except in
#   compliance with the License. You may obtain a copy of the License at
#   http://www.mozilla.org/MPL/
#
#   Software distributed under the License is distributed on an "AS IS"
#   basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
#   License for the specific language governing rights and limitations
#   under the License.
#
#   The Original Code is tDOM.
#
#   The Initial Developer of the Original Code is Jochen Loewer
#   Portions created by Jochen Loewer are Copyright (C) 1998, 1999
#   Jochen Loewer. All Rights Reserved.
#
#   Contributor(s):            
#
#
#   $Log$
#   Revision 1.2  2002/02/26 14:05:02  rolf
#   Updated the [load ...] to the new version number 0.7
#
#   Revision 1.1.1.1  2002/02/22 01:05:34  rolf
#   tDOM0.7test with Jochens first set of patches
#
#
#
#   written by Rolf Ade
#   August, 2001
#
#----------------------------------------------------------------------------

if {[catch {package require tdom} errMsg]} {
    if {[catch {
        load   [file dirname [info script]]/../unix/tdom0.7.so
        source [file dirname [info script]]/../lib/tdom.tcl
    }]} {
        puts $errMsg
    }
}


#----------------------------------------------------------------------------
#    externalEntityRefHandler
#
#----------------------------------------------------------------------------
proc externalEntityRefHandler { base systemId publicId } {

    if {[regexp {^[a-zA-Z]+:/} $systemId]}  {
        # Seems to be not relative to the base
        if {[regexp { *file://(.*)} $systemId dummy path]} {
            set fd [open $path]
            fconfigure $fd -translation binary
            return [list channel $systemId $fd]
        } else {
            return -code error  -errorinfo "externalEntityRefHandler: can only handle file URL's"
        }
    } else {
        if {$base == ""} {
            return -code error -errorinfo "externalEntityRefHandler: can't resolve relative URI - no base URI given!"
        }
        if {[regexp { *file://(.*)} $base dummy basepath]} {
            set basedir [file dirname $basepath]
            set entitypath "${basedir}/${systemId}"
            set fd [open $entitypath]
            fconfigure $fd -translation binary
            return [list channel "file://${basedir}/${systemId}" $fd]
        } else {
            return -code error  -errorinfo "externalEntityRefHandler: can only handle file URL's"
        }
    }
}


#----------------------------------------------------------------------------
#    begin of main part
#----------------------------------------------------------------------------

    if {[llength $argv] != 2 && [llength $argv] != 3} {
        puts stderr "usage: $argv0 <xml-file> <xslt-file> ?output_method (asHTML|asXML)?"
        exit 1
    }

    foreach { xmlFile xsltFile outputOpt } $argv break

    set xmlfd  [open $xmlFile  r]
    set xsltfd [open $xsltFile r]

    switch [file pathtype $xmlFile] {
        "relative" {
            set xmlbaseurl "file://[pwd]/$xmlFile"
        }
        default {
            set xmlbaseurl "file://$xmlFile"
        }
    }

    switch [file pathtype $xsltFile] {
        "relative" {
            set xsltbaseurl "file://[pwd]/$xsltFile"
        }
        default {
            set xsltbaseurl "file://$xsltFile"
        }
    }


    set xmldoc [dom parse -baseurl $xmlbaseurl \
                          -externalentitycommand externalEntityRefHandler \
                          [read $xmlfd [file size $xmlFile] ] ]

    set xsltdoc [dom parse -baseurl $xsltbaseurl \
                           -externalentitycommand externalEntityRefHandler \
                           -keepEmpties \
                           [read $xsltfd [file size $xsltFile] ] ]

     close $xmlfd
     close $xsltfd

     set xmlroot [$xmldoc documentElement]

     $xmlroot xslt $xsltdoc resultDoc

     set resultRoot [$resultDoc documentElement]
     if { $outputOpt != "" } {
         puts -nonewline [$resultRoot #outputOpt]
     } else {
         puts -nonewline [$resultRoot asHTML]
     }
     set nextRoot [$resultRoot nextSibling]
     while {$nextRoot != ""} {
         if { $outputOpt != "" } {
             puts -nonewline [$nextRoot $outputOpt]
         } else {
             puts -nonewline [$nextRoot asHTML]
         }
         set nextRoot [$nextRoot nextSibling]
     }
     puts ""

#----------------------------------------------------------------------------
#    end of main part
#----------------------------------------------------------------------------

