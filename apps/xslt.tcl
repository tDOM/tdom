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
#
#   written by Rolf Ade
#   August, 2001
#
#----------------------------------------------------------------------------

package require tdom 0.7.5
if {[lsearch [namespace children] ::tdom] == -1} {
    # tcldomsh without the script library. Source the lib.
    source [file join [file dir [info script]] ../lib tdom.tcl]
}


if {[llength $argv] != 2 && [llength $argv] != 3} {
    puts stderr "usage: $argv0 <xml-file> <xslt-file> \
                        ?output_method (asHTML|asXML|asText)?"
    exit 1
}

foreach { xmlFile xsltFile outputOpt } $argv break

set xmldoc [dom parse -baseurl [tDOM::baseURL $xmlFile] \
                      -externalentitycommand tDOM::extRefHandler \
                      -keepEmpties \
                      [tDOM::xmlReadFile $xmlFile] ]

dom setStoreLineColumn 1
set xsltdoc [dom parse -baseurl [tDOM::baseURL $xsltFile] \
                       -externalentitycommand tDOM::extRefHandler \
                       -keepEmpties \
                       [tDOM::xmlReadFile $xsltFile] ]
dom setStoreLineColumn 0

set xmlroot [$xmldoc documentElement]

$xmlroot xslt $xsltdoc resultDoc


if {$outputOpt == ""} {
    set outputOpt [$resultDoc getDefaultOutputMethod]
}

switch $outputOpt {
    asXML -
    xml  {
        puts [$resultDoc asXML]
    }
    asHTML -
    html {
        puts [$resultDoc asHTML]
    }
    asText -
    text {
        set resultRoot [$resultDoc documentElement]
        puts [$node nodeValue]
    }
    default {
        puts stderr "Unknown output method '$outputOpt'!"
        exit 1
    }
}

