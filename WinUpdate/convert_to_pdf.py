#!/usr/bin/env python3
"""Convert ODT manuals to PDF using LibreOffice"""

import subprocess
import os
import time

def convert_to_pdf(odt_file):
    """Convert ODT file to PDF using LibreOffice headless mode"""
    if not os.path.exists(odt_file):
        print(f"❌ File not found: {odt_file}")
        return False
    
    libreoffice_path = r"C:\Program Files\LibreOffice\program\soffice.exe"
    
    if not os.path.exists(libreoffice_path):
        print(f"❌ LibreOffice not found at: {libreoffice_path}")
        return False
    
    print(f"Converting {odt_file} to PDF...")
    
    # Run LibreOffice in headless mode to convert to PDF
    try:
        result = subprocess.run([
            libreoffice_path,
            "--headless",
            "--convert-to", "pdf",
            "--outdir", ".",
            odt_file
        ], capture_output=True, text=True, timeout=60)
        
        if result.returncode == 0:
            pdf_file = odt_file.replace('.odt', '.pdf')
            if os.path.exists(pdf_file):
                print(f"✓ Created {pdf_file}")
                return True
            else:
                print(f"❌ PDF file was not created")
                return False
        else:
            print(f"❌ Conversion failed: {result.stderr}")
            return False
            
    except subprocess.TimeoutExpired:
        print(f"❌ Conversion timed out")
        return False
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

if __name__ == "__main__":
    files_to_convert = [
        "WinUpdate_Brukermanual_NO.odt",
        "WinUpdate_User_Manual_UK.odt",
        "WinUpdate_Användarmanual_SE.odt"
    ]
    
    print("Converting ODT files to PDF...\n")
    
    success_count = 0
    for odt_file in files_to_convert:
        if convert_to_pdf(odt_file):
            success_count += 1
        time.sleep(1)  # Small delay between conversions
        print()
    
    print(f"\n✓ Successfully converted {success_count}/{len(files_to_convert)} files to PDF")
