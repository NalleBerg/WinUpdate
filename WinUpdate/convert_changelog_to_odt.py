#!/usr/bin/env python3
"""Convert Changelog.html to ODT format"""

from odf.opendocument import OpenDocumentText
from odf.style import Style, TextProperties, ParagraphProperties
from odf.text import P, H, List, ListItem, A
from html.parser import HTMLParser
import sys

class ChangelogHTMLParser(HTMLParser):
    def __init__(self, doc):
        super().__init__()
        self.doc = doc
        self.current_text = []
        self.in_h3 = False
        self.in_p = False
        self.in_li = False
        self.in_strong = False
        self.in_ul = False
        self.list_items = []
        self.h3_attrs = {}
        
        # Create styles
        self.create_styles()
        
    def create_styles(self):
        # Heading style (red, centered)
        self.heading_style = Style(name="Heading3Red", family="paragraph")
        self.heading_style.addElement(ParagraphProperties(textalign="center"))
        heading_text_props = TextProperties(color="#ff0000", fontsize="14pt", fontweight="bold")
        self.heading_style.addElement(heading_text_props)
        self.doc.styles.addElement(self.heading_style)
        
        # Bold style
        self.bold_style = Style(name="Bold", family="text")
        self.bold_style.addElement(TextProperties(fontweight="bold"))
        self.doc.styles.addElement(self.bold_style)
        
        # Normal paragraph style
        self.normal_style = Style(name="Normal", family="paragraph")
        self.doc.styles.addElement(self.normal_style)
        
    def handle_starttag(self, tag, attrs):
        if tag == 'h3':
            self.in_h3 = True
            self.current_text = []
            self.h3_attrs = dict(attrs)
        elif tag == 'p':
            self.in_p = True
            self.current_text = []
        elif tag == 'ul':
            self.in_ul = True
            self.list_items = []
        elif tag == 'li':
            self.in_li = True
            self.current_text = []
        elif tag == 'strong':
            self.in_strong = True
        elif tag == 'hr':
            # Add horizontal line (empty paragraph with bottom border)
            p = P(stylename=self.normal_style)
            self.doc.text.addElement(p)
            
    def handle_endtag(self, tag):
        if tag == 'h3' and self.in_h3:
            self.in_h3 = False
            text = ''.join(self.current_text).strip()
            if text and text != '\xa0':  # Skip &nbsp;
                h = H(outlinelevel=3, stylename=self.heading_style)
                h.addText(text)
                self.doc.text.addElement(h)
            self.current_text = []
            
        elif tag == 'p' and self.in_p:
            self.in_p = False
            text = ''.join(self.current_text).strip()
            if text:
                p = P(stylename=self.normal_style)
                p.addText(text)
                self.doc.text.addElement(p)
            self.current_text = []
            
        elif tag == 'ul' and self.in_ul:
            self.in_ul = False
            # Add all list items
            if self.list_items:
                for item_text in self.list_items:
                    p = P(stylename=self.normal_style)
                    p.addText("• " + item_text)
                    self.doc.text.addElement(p)
            self.list_items = []
            
        elif tag == 'li' and self.in_li:
            self.in_li = False
            text = ''.join(self.current_text).strip()
            if text:
                self.list_items.append(text)
            self.current_text = []
            
        elif tag == 'strong':
            self.in_strong = False
            
    def handle_data(self, data):
        if self.in_h3 or self.in_p or self.in_li:
            self.current_text.append(data)

def convert_html_to_odt(html_file, odt_file):
    """Convert HTML changelog to ODT format"""
    # Create ODT document
    doc = OpenDocumentText()
    
    # Read HTML content
    with open(html_file, 'r', encoding='utf-8') as f:
        html_content = f.read()
    
    # Parse and convert
    parser = ChangelogHTMLParser(doc)
    parser.feed(html_content)
    
    # Save ODT
    doc.save(odt_file)
    print(f"Successfully created {odt_file}")

if __name__ == "__main__":
    html_file = "Changelog.html"
    odt_file = "Changelog.odt"
    
    try:
        convert_html_to_odt(html_file, odt_file)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)
