<?xml version="1.0" encoding="UTF-8"?>
<html xsl:version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<body style="font-family:Helvetica;font-size:11pt;">

<table border="1">

<xsl:for-each select="owntracks/point">
  <tr>
    <td><xsl:value-of select="isotst"/></td>
    <td><xsl:value-of select="tid"/></td>
    <td><xsl:value-of select="cog"/></td>
    <td><xsl:value-of select="vel"/></td>
    <td><xsl:value-of select="alt"/></td>
    <td><xsl:value-of select="lat"/>, <xsl:value-of select="lon"/></td>
    <td><xsl:value-of select="ghash"/></td>
    <td><xsl:value-of select="addr"/></td>

  </tr>
</xsl:for-each>

</table>

</body>
</html>
