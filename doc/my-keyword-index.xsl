<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version="1.0">

  <xsl:import href="navpages.xsl"/>

  <xsl:output method="html"
    doctype-public="-//W3C//DTD HTML 4.0 Transitional//EN"
    indent	= "yes"
    encoding	= "us-ascii"/>

  <xsl:variable name="removeChars" select="' &#x9;&#xA;%*()[];'"/>
  <xsl:variable name="lower" select="'abcdefghijklmnopqrstuvwxyz'"/>
  <xsl:variable name="upper" select="'ABCDEFGHIJKLMNOPQRSTUVWXYZ'"/>
  <xsl:variable name="keywords" select="/INDEX/KWD"/>

  <xsl:template name="header">
    <xsl:call-template name="navigation-page-header">
      <xsl:with-param name="iam" select="'KWL'"/>
    </xsl:call-template>
  </xsl:template>


  <xsl:template match="INDEX">
    <html>
      <head>
        <title><xsl:value-of select="@title"/>: Keyword Index</title>
        <link rel="stylesheet" href="manpage.css"/>
        <meta name="xsl-processor" content="{system-property('xsl:vendor')}"/>
        <meta name="generator" content="$RCSfile$ $Revision$"/>
      </head>
      <body>
        <DIV class="header">
          <xsl:call-template name="header"/>
          <xsl:call-template name="keyword-header"/>
        </DIV>
        <DIV class="body">
          <DIV class="table">
            <TABLE width="100%">
              <xsl:call-template name="keyword-list"/>
            </TABLE>
          </DIV>
        </DIV>
        <DIV class="footer">
          <xsl:call-template name="footer"/>
        </DIV>
      </body>
    </html>
  </xsl:template>

  <xsl:template name="keyword-header">
    <DIV class="navbar">
      <xsl:for-each select="$keywords">
        <xsl:sort select="@name"/>
        <xsl:variable name="indexChar" select="translate(substring(normalize-space(@name),1,1),$lower,$upper)"/>
        <xsl:if test="generate-id($keywords[$indexChar=translate(normalize-space(substring(@name,1,1)),$lower,$upper)][1])=generate-id(.)">
          <a href="#KEYWORDs-{$indexChar}">
            <xsl:value-of select="$indexChar"/>
          </a>
          <xsl:value-of select="$navsep"/>
        </xsl:if>
      </xsl:for-each>
    </DIV>      
  </xsl:template>

  <xsl:template name="keyword-list">
    <xsl:for-each select="$keywords">
      <xsl:variable name="indexChar" select="translate(substring(normalize-space(@name),1,1),$lower,$upper)"/>
      <xsl:if test="generate-id($keywords[$indexChar=translate(substring(normalize-space(@name),1,1),$lower,$upper)][1])=generate-id(.)">
        <tr class="header">
          <th colspan="2">
            <a name="KEYWORDs-{$indexChar}">Keywords: <xsl:value-of select="$indexChar"/></a>
          </th>
        </tr>
      </xsl:if>
      <tr class="row{position() mod 2}">
        <td width="35%">
          <a name="KW-{translate(@name,$removeChars,'')}">
            <xsl:value-of select="@name"/>
          </a>
        </td>
        <td width="65%">
          <xsl:for-each select="refto">
            <xsl:if test="position() > 1">
              <xsl:value-of select="$navsep"/>
            </xsl:if>
            <a href="{.}.html"><xsl:value-of select="."/></a>
          </xsl:for-each>
        </td>
      </tr>
    </xsl:for-each>
  </xsl:template>

</xsl:stylesheet>




