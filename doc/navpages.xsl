<?xml version='1.0'?>
<!--
	$Id$
	Author: 	Joe English, <jenglish@flightlab.com>
	Created: 	29 Jun 2000
	Description:	Common XSL code for navigational material
-->
<xsl:stylesheet
    xmlns:xsl	= "http://www.w3.org/1999/XSL/Transform"
    xmlns:data	= "data"
    version	= "1.0"
    exclude-result-prefixes="data"
>
<xsl:variable name="navsep"> &#183; </xsl:variable>
<xsl:variable name="stylesheetData" select="document('')//data:data"/>


<!-- template navigation-page-header: -->

<xsl:template name="navigation-page-header">
    <xsl:param name="iam"/>
    <h1 class="title" align="center"
	><xsl:value-of select="/*/title | /*/@title"
	/>: <xsl:value-of select="$stylesheetData//navpage[@id=$iam]/@title"
    /></h1>
    <p class="navaid" align="center">
	<xsl:for-each select="$stylesheetData//navpages/navpage"
	    ><a class="navaid" href="{@href}"><xsl:value-of select="@title"/></a
	    ><xsl:if test="following-sibling::navpage"
	    	><xsl:value-of select="$navsep"
	    /></xsl:if
	></xsl:for-each>
    </p>
    <hr class="navsep"/>
</xsl:template>

<xsl:template name="footer">
    <hr class="navsep"/>
    <xsl:copy-of select="document('footer.xml')"/>
</xsl:template>

<data:data>
<navpages>
  <navpage id="TOC" title="Table of Contents"	href="index.html"/>
  <navpage id="CAT" title="Index"		href="category-index.html"/>
  <navpage id="KWL" title="Keywords"		href="keyword-index.html"/>
</navpages>
</data:data>

<!-- Default catch-all rule, used to catch source elements
     for which there is no matching template:
     Usage: 
     	template match="*" priority="-1" 
	    call-template name="catchall"
-->
<xsl:variable name="debug" select="1"/>
<xsl:template name="catchall"
    ><xsl:message terminate="no">Unrecognized GI <xsl:value-of select="name()"
    /></xsl:message
    ><xsl:choose
	><xsl:when test="$debug"
	    ><xsl:element name="EM"
		><xsl:attribute name="class">UNRECOGNIZED</xsl:attribute
		><xsl:processing-instruction name="UNRECOGNIZED"
		    ><xsl:value-of select="name()"
		/></xsl:processing-instruction
		><xsl:apply-templates
	    /></xsl:element
	></xsl:when
	><xsl:otherwise><xsl:apply-templates/></xsl:otherwise
    ></xsl:choose
></xsl:template>

</xsl:stylesheet>
