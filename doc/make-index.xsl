<?xml version='1.0'?>
<!--
	$Id$
	Author: 	Joe English, <jenglish@flightlab.com>
	Created: 	2 Aug 2000; rewritten from makemap.xsl 10 Jul 2000
	Description:	Build INDEX.MAP from a <manual> or <manpage>.
-->

<xsl:stylesheet
    xmlns:xsl	= "http://www.w3.org/1999/XSL/Transform"
    version	= "1.0"
>
<xsl:output method="xml"
    doctype-public="-//jenglish//DTD TMML 0.5//EN"
    indent	= "yes"
/>
<xsl:strip-space
    elements	= "meta extensions xlh"
/>

<xsl:variable name="removeChars" select="' &#x9;&#xA;%*()[]'"/>
<xsl:variable name="lower" select="'abcdefghijklmnopqrstuvwxyz'"/>
<xsl:variable name="upper" select="'ABCDEFGHIJKLMNOPQRSTUVWXYZ'"/>
<xsl:variable name="keywordNodes" select="//keyword"/>

<xsl:template match="/*" priority="3">
    <xsl:value-of select="'&#xA;'"/>
    <xsl:comment> This file was automatically generated </xsl:comment>
    <xsl:value-of select="'&#xA;'"/>
    <xsl:element name="INDEX">
	<xsl:attribute name="title"><xsl:value-of select="title | @title"
	/></xsl:attribute>
	<xsl:attribute name="package"><xsl:value-of select="@package"
	/></xsl:attribute>
	<xsl:apply-templates/>
        <xsl:call-template name="keywords"/>
    </xsl:element>
</xsl:template>

<!-- Transcribe metainformation from the the manual header 
     into the index:
-->
<xsl:template match="meta//node()">
    <xsl:copy
    	><xsl:for-each select="@*"><xsl:copy/></xsl:for-each
	><xsl:apply-templates
    /></xsl:copy>
</xsl:template>

<!-- Recursively expand subdocuments:
-->
<xsl:template match="subdoc" priority="2">
    <xsl:for-each select="document(@href)/manpage">
	<xsl:call-template name="manpage"/>
    </xsl:for-each>
</xsl:template>

<xsl:template name="manpage" match="manpage" priority="2">
    <MAN id="{@id}" title="{@title}"/>
    <DEF cat="manpage" name="{@id}" manpage="{@id}"/>
    <xsl:apply-templates/>
</xsl:template>

<xsl:template match="namesection/name" priority="2">
    <xsl:element name="DEF">
	<xsl:attribute name="cat"
	    ><xsl:value-of select="ancestor::manpage/@cat"
	/></xsl:attribute>
	<xsl:attribute name="name"
	    ><xsl:value-of select="."
	/></xsl:attribute>
	<xsl:attribute name="manpage"
	    ><xsl:value-of select="ancestor::manpage/@id"
	/></xsl:attribute>
    </xsl:element>
</xsl:template>

<xsl:template match="optionlist[@scope='global']/optiondef " priority="2">
    <xsl:variable name="cat"
    	><xsl:choose
	    ><xsl:when test="@cat"><xsl:value-of select="@cat"
	    /></xsl:when
	    ><xsl:otherwise>option</xsl:otherwise
	></xsl:choose
    ></xsl:variable>
    <xsl:for-each select="./name">
	<xsl:variable name="name"
	    ><xsl:choose
		><xsl:when test="@name"><xsl:value-of select="@name"
		/></xsl:when
		><xsl:otherwise><xsl:value-of select="."
		/></xsl:otherwise
	    ></xsl:choose
	></xsl:variable>
	<DEF cat    = "{ancestor-or-self::*/@cat}"
	     name   = "{$name}"
	     manpage= "{ancestor::manpage/@id}"
	     subpart= 
	 "{ancestor-or-self::*/@cat}_{translate($name,$removeChars,'')}"
	/>
    </xsl:for-each>
</xsl:template>

<xsl:template match="syntax[@name and @cat]" priority="2">
    <DEF
	cat 	= "{@cat}"
	name 	= "{@name}"
	manpage	= "{ancestor::manpage/@id}"
	subpart	= "{@cat}_{@name}"
    />
</xsl:template>

<xsl:template match="dl[@scope='global' and @cat]/dle/dt" priority="2">
    <DEF
	cat 	= "{parent::*/@cat}"
	name 	= "{.}"
	manpage	= "{ancestor::manpage/@id}"
	subpart	= "{parent::*/@cat}_{translate(.,$removeChars,'')}"
    />
</xsl:template>

<xsl:template match="//*[@tmml='xl' and @cat and not(@scope='local')]" priority="1.5">
    <xsl:variable name="cat" select="@cat"/>
    <xsl:for-each select="./descendant::name">
	<DEF 
	    cat 	= "{$cat}"
	    name	= "{.}"
	    manpage	= "{ancestor::manpage/@id}"
	    subpart	= "{$cat}_{translate(.,$removeChars,'')}"
	/>
    </xsl:for-each>
</xsl:template>

<xsl:template name="buildKWD">
  <xsl:param name="keyword"/>
  <xsl:param name="keyword-literal"/>
  <KWD name="{$keyword-literal}">
    <xsl:for-each select="$keywordNodes[$keyword=(translate(normalize-space(.),$lower,$upper))]">
      <xsl:sort select="ancestor::manpage/@id"/>
      <refto><xsl:value-of select="ancestor::manpage/@id"/></refto>
    </xsl:for-each>
  </KWD>
</xsl:template>  

<xsl:template name="keywords">
  <xsl:for-each select="$keywordNodes">
    <xsl:sort/>
    <xsl:if test="generate-id($keywordNodes[translate(normalize-space(.),$lower,$upper)=translate(normalize-space(current()),$lower,$upper)][1])=generate-id(.)">
      <xsl:call-template name="buildKWD">
        <xsl:with-param name="keyword" select="translate(normalize-space(.),$lower,$upper)"/>
        <xsl:with-param name="keyword-literal" select="."/>
      </xsl:call-template>
    </xsl:if>
  </xsl:for-each>
</xsl:template>


<!-- By default, names in ARGLISTs and OPTIONLISTs aren't global -->
<xsl:template match = 
	"arglist[not(@scope = 'global')] | optionlist[not(@scope = 'global')]"
	priority="2"
/>

<!-- Default templates:
-->
<xsl:template match="name | *[@tmml = 'name']" priority="0.5">
    <xsl:message>Unclassified name <xsl:value-of select="."
    /> in <xsl:for-each select="ancestor-or-self::*"
    	><xsl:value-of select="name()"
	/>/</xsl:for-each>
    </xsl:message>
</xsl:template>

<xsl:template match="*" priority="0">
    <xsl:apply-templates/>
</xsl:template>
<xsl:template match="node()" priority="-0.5"/>

</xsl:stylesheet>
