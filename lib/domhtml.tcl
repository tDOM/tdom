#----------------------------------------------------------------------------
#   Copyright (c) 1999,2000 Jochen Loewer (loewerj@hotmail.com)   
#----------------------------------------------------------------------------
#
#   $Header$
#
#
#   Implements simple HTML layer on top of core DOM Level-1 specification,
#   as implemented in tDOM0.4 package by Jochen Loewer (loewerj@hotmail.com)
# 
#   No error checking is performed. This should be delegated to some
#   other level (internal DOM DTD checking probably ?).
#    
#   Not all HTML elements are implemented, but it's fairly simple to
#   add them as needed. A short usage example is at the bottom of file.
#
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
#       3 Apr 2000   Zoran Vasiljevic (zoran@v-connect.com)
#                    Initial domHTML idea
#
#   $Log$
#   Revision 1.1  2002/02/22 01:05:35  rolf
#   Initial revision
#
#
#
#   written by Zoran Vasiljevic / Jochen Loewer
#   April, 2000      
#
#----------------------------------------------------------------------------


package require tdom



#-----------------------------------------------------------------------------
#                            Utility procedures
#-----------------------------------------------------------------------------

namespace eval ::dom::domHTML {

    variable ownerDocument {}; # the current document
    variable parent        {}; # the current parent element
}


#-----------------------------------------------------------------------------
#  _elm  -  Generate named element node. Ignores the very last argument.
#           Called only within the element-with-body procedure "eb".
#
#-----------------------------------------------------------------------------
proc ::dom::domHTML::_elm { name args } {

    set obj [$::dom::domHTML::ownerDocument createElement $name]
    $::dom::domHTML::parent appendChild $obj

    eval $obj setAttribute [lrange $args 0 [expr {[llength $args] - 2}]]

    return $obj
}


#-----------------------------------------------------------------------------
#   el  -  Generate named element node w/o associated body
#
#-----------------------------------------------------------------------------
proc ::dom::domHTML::el { name args } {

    set obj [$::dom::domHTML::ownerDocument createElement $name]
    $::dom::domHTML::parent appendChild $obj

    eval $obj setAttribute $args
    return $obj  
}


#-----------------------------------------------------------------------------
#   eb  -  Generate named element node with associated body.
#
#-----------------------------------------------------------------------------
proc ::dom::domHTML::eb { name args } {

    set ::dom::domHTML::parent [eval ::dom::domHTML::_elm $name $args]
    uplevel 2 [lindex $args end]
    set ::dom::domHTML::parent [$::dom::domHTML::parent parentNode]
}


#-----------------------------------------------------------------------------
#   HTML  -  Creates HTML document.
#            One should use "$doc documentElement" to fetch the "<HTML>" 
#            node and print it, and/or "$doc delete" to reclaim memory 
#            used by the tree.
#
#      See usage example at the end of file...
#
#-----------------------------------------------------------------------------
proc HTML {args} {

    #
    # Create the HTML document
    #
    
    set doc [dom createDocument html]

    set ::dom::domHTML::ownerDocument $doc

    #
    # Push its top-level element on stack
    # and evaluate body in caller's context
    #

    set ::dom::domHTML::parent [$doc documentElement]
    uplevel [lindex $args end]
    set ::dom::domHTML::parent [$::dom::domHTML::parent parentNode]

    #
    # Returns document node. 
    #

    return $doc
}


#-----------------------------------------------------------------------------
#                          HTML element commands
#-----------------------------------------------------------------------------

#-----------------------------------------------------------------
#   Elements appearing outside of the document body
#-----------------------------------------------------------------

proc HEAD  {args} {eval ::dom::domHTML::eb  head  $args}
proc TITLE {args} {eval ::dom::domHTML::eb  title $args}
proc BODY  {args} {eval ::dom::domHTML::eb  body  $args}


#-----------------------------------------------------------------
#   Link elements
#-----------------------------------------------------------------

proc A    {h args} {eval ::dom::domHTML::eb  a    href $h  $args}
proc LINK {h args} {eval ::dom::domHTML::el  link href $h  $args}
proc BASE {h args} {eval ::dom::domHTML::el  base href $h  $args}


#-----------------------------------------------------------------
#   Object, image elements
#-----------------------------------------------------------------

proc IMG    {s args} {eval ::dom::domHTML::el  img   src $s  $args}
proc MAP    {n args} {eval ::dom::domHTML::eb  map   name $n $args}
proc OBJECT {args}   {eval ::dom::domHTML::eb  object        $args}
proc PARAM  {args}   {eval ::dom::domHTML::el  param         $args}
proc AREA   {args}   {eval ::dom::domHTML::el  area          $args}


#-----------------------------------------------------------------
#   List elements
#-----------------------------------------------------------------

proc UL {args} {eval ::dom::domHTML::eb  ul  $args}
proc OL {args} {eval ::dom::domHTML::eb  ol  $args}
proc LI {args} {eval ::dom::domHTML::eb  li  $args}
proc DL {args} {eval ::dom::domHTML::eb  dl  $args}
proc DT {args} {eval ::dom::domHTML::eb  dt  $args}
proc DD {args} {eval ::dom::domHTML::eb  dd  $args}


#-----------------------------------------------------------------
#   Table elements
#-----------------------------------------------------------------

proc TABLE {args} {eval ::dom::domHTML::eb table $args}
proc TR    {args} {eval ::dom::domHTML::eb tr    $args}
proc TH    {args} {eval ::dom::domHTML::eb th    $args}
proc TD    {args} {eval ::dom::domHTML::eb td    $args}


#-----------------------------------------------------------------
#   Form elements
#-----------------------------------------------------------------

proc FORM     {a args}     {eval ::dom::domHTML::eb form     action $a method post $args}
proc FORMGET  {a args}     {eval ::dom::domHTML::eb form     action $a method get  $args}
proc INPUT    {t n v args} {eval ::dom::domHTML::el input    type $t name $n value $v $args}
proc BUTTON   {t n args}   {eval ::dom::domHTML::el buttonn  type $t name $n $args}
proc SELECT   {n args}     {eval ::dom::domHTML::eb seelct   name $n $args}
proc OPTION   {args}       {eval ::dom::domHTML::eb option   $args}
proc TEXTAREA {n r c args} {eval ::dom::domHTML::eb textarea name $n rows $r cols $c $args}


#-----------------------------------------------------------------
#   Text elements
#-----------------------------------------------------------------

proc HR {args} {eval ::dom::domHTML::el  hr  $args}
proc BR {args} {eval ::dom::domHTML::el  br  $args}
proc P  {args} {eval ::dom::domHTML::eb  p   $args}
proc TT {args} {eval ::dom::domHTML::eb  tt  $args}
proc I  {args} {eval ::dom::domHTML::eb  i   $args}
proc B  {args} {eval ::dom::domHTML::eb  b   $args}
proc H1 {args} {eval ::dom::domHTML::eb  h1  $args}
proc H2 {args} {eval ::dom::domHTML::eb  h2  $args}
proc H3 {args} {eval ::dom::domHTML::eb  h3  $args}
proc H4 {args} {eval ::dom::domHTML::eb  h4  $args}
proc H5 {args} {eval ::dom::domHTML::eb  h5  $args}
proc H6 {args} {eval ::dom::domHTML::eb  h6  $args}

proc BIG    {args} {eval ::dom::domHTML::eb  big     $args}
proc SMALL  {args} {eval ::dom::domHTML::eb  small   $args}
proc STRONG {args} {eval ::dom::domHTML::eb  string  $args}
proc PRE    {args} {eval ::dom::domHTML::eb  pre     $args}


#-----------------------------------------------------------------
#   Style elements
#-----------------------------------------------------------------

proc STYLE {args} {eval ::dom::domHTML::eb  style  type text/css $args}

# ...
# to be continued
# ...



#-----------------------------------------------------------------------------
#         *NOT* part of HTML but handy for constructing text element nodes
#
#   T  -  Generate new text element node and append it to parent
#
#-----------------------------------------------------------------------------
proc T {text} {

    set textNode [$::dom::domHTML::ownerDocument createTextNode $text]
    $::dom::domHTML::parent appendChild $textNode
}

#-----------------------------------------------------------------------------
#   M  -  Generate new subtree out of XML markup and append it to parent
#
#-----------------------------------------------------------------------------
proc M {markup} {

    $::dom::domHTML::parent appendXML $markup
}


#-----------------------------------------------------------------------------
#   Short usage example .... No need to worry about closing HTML tags and
#   debugging ugly ASP, ADP, PHP, JSP, LiveWire, whatever, pages....
#   Tcl parser and tDOM care about everything !!!
#
#-----------------------------------------------------------------------------
if 0 {
    set doc [HTML {
        TITLE {T "Test document generated with tDOM0.4"}
        BODY {
          TABLE border 1 width 100 {

              # -- make 5 rows with 2 columns...
              for {set i 0} {$i < 5} {incr i} {
                  TR {
                      # -- use XML shortcut...
                      TD { M "<i>italic $i and <b>italic-bold $i</b></i>"}
            
                      # -- or write with nodes...
                      TD { I {T "italic $i and "; B {T "italic-bold $i"}}}
                  }
              }
          }
        }
    }]
    puts stdout [[$doc documentElement] asXML]; $doc delete
}
