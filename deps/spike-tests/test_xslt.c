#include <string.h>
#include <libxml/parser.h>
#include <libxslt/xslt.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>
#include <stdio.h>
int main(void) {
    const char *xml = "<root><item>hi</item></root>";
    const char *xsl =
        "<xsl:stylesheet version=\"1.0\" xmlns:xsl=\"http://www.w3.org/1999/XSL/Transform\">"
        "<xsl:template match=\"/\"><out><xsl:value-of select=\"root/item\"/></out></xsl:template>"
        "</xsl:stylesheet>";
    xmlDocPtr xmlDoc = xmlReadMemory(xml, (int)strlen(xml), "xml.xml", NULL, 0);
    xmlDocPtr xslDoc = xmlReadMemory(xsl, (int)strlen(xsl), "xsl.xml", NULL, 0);
    if (!xmlDoc || !xslDoc) { printf("parse failed\n"); return 1; }
    xsltStylesheetPtr sheet = xsltParseStylesheetDoc(xslDoc);
    xmlDocPtr res = xsltApplyStylesheet(sheet, xmlDoc, NULL);
    xmlChar *out; int outlen;
    xsltSaveResultToString(&out, &outlen, res, sheet);
    printf("xslt OK: %s\n", out ? (char*)out : "(null)");
    if (out) xmlFree(out);
    xsltFreeStylesheet(sheet);
    xmlFreeDoc(xmlDoc);
    xmlFreeDoc(res);
    return 0;
}
