<?xml version="1.0" encoding="UTF-8"?>
<!--
SPDX-FileCopyrightText: 2026 Orcinus contributors
SPDX-License-Identifier: GPL-2.0-or-later

Converts Asciidoctor's DocBook 5 output into the KDE DocBook 4.5 dialect, so that the
Orcinus chapters can be assembled with Dolphin's handbook (.privatedoc/handbook-plan.md).
Run with: meinproc6 - -stylesheet db5-to-db45.xsl - -stdout input.xml
-->
<xsl:stylesheet version="1.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:db="http://docbook.org/ns/docbook"
                xmlns:xl="http://www.w3.org/1999/xlink"
                exclude-result-prefixes="db xl">

  <xsl:output method="xml" encoding="UTF-8" indent="no"/>

  <!-- Elements: the same name without the DocBook 5 namespace. -->
  <xsl:template match="*">
    <xsl:element name="{local-name()}">
      <xsl:apply-templates select="@*|node()"/>
    </xsl:element>
  </xsl:template>

  <!-- Attributes without a namespace are kept. -->
  <xsl:template match="@*">
    <xsl:copy/>
  </xsl:template>

  <xsl:template match="@xml:id">
    <xsl:attribute name="id"><xsl:value-of select="."/></xsl:attribute>
  </xsl:template>

  <xsl:template match="@xml:lang">
    <xsl:attribute name="lang"><xsl:value-of select="."/></xsl:attribute>
  </xsl:template>

  <!-- DocBook 5 only. -->
  <xsl:template match="@version|@xl:*|@xml:base"/>

  <!-- External links: DocBook 4.5 has ulink for them. -->
  <xsl:template match="db:link[@xl:href]">
    <ulink url="{@xl:href}">
      <xsl:apply-templates select="node()"/>
    </ulink>
  </xsl:template>

  <!-- The book info is not used: the frame (index.docbook.in) has its own bookinfo. -->
  <xsl:template match="db:book/db:info"/>

  <!-- Chapter and section info only carries the title in Asciidoctor's output. -->
  <xsl:template match="db:info">
    <xsl:apply-templates select="db:title|db:subtitle|db:titleabbrev"/>
  </xsl:template>

  <!-- Asciidoctor's processing instructions (asciidoc-toc, asciidoc-numbered). -->
  <xsl:template match="processing-instruction()"/>

</xsl:stylesheet>
