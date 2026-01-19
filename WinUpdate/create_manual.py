#!/usr/bin/env python3
"""Create WinUpdate user manual in ODT format"""

from odf.opendocument import OpenDocumentText
from odf.style import Style, TextProperties, ParagraphProperties, ListLevelProperties
from odf.text import P, H, List, ListItem, Span
from odf.draw import Frame, Image as ODFImage

def create_manual(filename, language='no'):
    """Create user manual in specified language"""
    
    doc = OpenDocumentText()
    
    # Create styles
    # Title style
    title_style = Style(name="Title", family="paragraph")
    title_style.addElement(ParagraphProperties(textalign="center", marginbottom="0.5cm"))
    title_style.addElement(TextProperties(fontsize="24pt", fontweight="bold", color="#0066cc"))
    doc.styles.addElement(title_style)
    
    # Heading 1 style
    h1_style = Style(name="Heading1", family="paragraph")
    h1_style.addElement(ParagraphProperties(margintop="0.5cm", marginbottom="0.3cm"))
    h1_style.addElement(TextProperties(fontsize="18pt", fontweight="bold", color="#003366"))
    doc.styles.addElement(h1_style)
    
    # Heading 2 style
    h2_style = Style(name="Heading2", family="paragraph")
    h2_style.addElement(ParagraphProperties(margintop="0.4cm", marginbottom="0.2cm"))
    h2_style.addElement(TextProperties(fontsize="14pt", fontweight="bold", color="#0066cc"))
    doc.styles.addElement(h2_style)
    
    # Heading 3 style
    h3_style = Style(name="Heading3", family="paragraph")
    h3_style.addElement(ParagraphProperties(margintop="0.3cm", marginbottom="0.15cm"))
    h3_style.addElement(TextProperties(fontsize="12pt", fontweight="bold", color="#666666"))
    doc.styles.addElement(h3_style)
    
    # Normal paragraph style
    normal_style = Style(name="Normal", family="paragraph")
    normal_style.addElement(ParagraphProperties(marginbottom="0.2cm", textalign="justify"))
    normal_style.addElement(TextProperties(fontsize="11pt"))
    doc.styles.addElement(normal_style)
    
    # Bold text style
    bold_style = Style(name="Bold", family="text")
    bold_style.addElement(TextProperties(fontweight="bold"))
    doc.styles.addElement(bold_style)
    
    # Italic text style
    italic_style = Style(name="Italic", family="text")
    italic_style.addElement(TextProperties(fontstyle="italic"))
    doc.styles.addElement(italic_style)
    
    # Code style
    code_style = Style(name="Code", family="text")
    code_style.addElement(TextProperties(fontfamily="Courier New", fontsize="10pt", color="#cc0000"))
    doc.styles.addElement(code_style)
    
    # List style
    list_style = Style(name="ListItem", family="paragraph")
    list_style.addElement(ParagraphProperties(marginleft="0.5cm", marginbottom="0.1cm"))
    doc.styles.addElement(list_style)
    
    # Note style (for tips/warnings)
    note_style = Style(name="Note", family="paragraph")
    note_style.addElement(ParagraphProperties(marginleft="0.5cm", marginright="0.5cm", 
                                              margintop="0.2cm", marginbottom="0.2cm",
                                              backgroundcolor="#f0f8ff", padding="0.3cm"))
    note_style.addElement(TextProperties(fontsize="10pt", fontstyle="italic"))
    doc.styles.addElement(note_style)
    
    if language == 'no':
        # Norwegian Manual
        # Title
        h = H(outlinelevel=1, stylename=title_style)
        h.addText("WinUpdate")
        doc.text.addElement(h)
        
        h = H(outlinelevel=1, stylename=title_style)
        h.addText("Brukermanual")
        doc.text.addElement(h)
        
        # Empty line
        doc.text.addElement(P(stylename=normal_style))
        
        # Version info
        p = P(stylename=normal_style)
        p.addText("Versjon: 2026.01.19")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("Sist oppdatert: 19. januar 2026")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 1. Introduction
        h = H(outlinelevel=1, stylename=h1_style)
        h.addText("1. Innledning")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("WinUpdate er et moderne Windows-program som forenkler administrasjon av programvareoppdateringer på din datamaskin. Programmet fungerer som et brukervennlig grensesnitt for Windows Package Manager (winget) og gir deg full kontroll over hvilke programmer som skal oppdateres og når.")
        doc.text.addElement(p)
        
        # 1.1 Key features
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("1.1 Hovedfunksjoner")
        doc.text.addElement(h)
        
        p = P(stylename=list_style)
        p.addText("• Automatisk søking etter tilgjengelige oppdateringer")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Oversiktlig liste over alle tilgjengelige oppdateringer")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Mulighet til å hoppe over spesifikke oppdateringer")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Systemstatusikon med periodisk skanning")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Flerspråklig støtte (norsk, engelsk, svensk)")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Visuell fremdriftsindikator under installasjon")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 1.2 System requirements
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("1.2 Systemkrav")
        doc.text.addElement(h)
        
        p = P(stylename=list_style)
        p.addText("• Windows 10 eller nyere")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Windows Package Manager (winget) installert")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Administratorrettigheter for installasjon av oppdateringer")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 2. Getting Started
        h = H(outlinelevel=1, stylename=h1_style)
        h.addText("2. Komme i gang")
        doc.text.addElement(h)
        
        # 2.1 First start
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("2.1 Første oppstart")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Når du starter WinUpdate for første gang, vil programmet automatisk søke etter tilgjengelige oppdateringer. Dette kan ta noen sekunder avhengig av hvor mange programmer du har installert.")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 2.2 Main window
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("2.2 Hovedvinduet")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Hovedvinduet i WinUpdate viser en oversikt over alle tilgjengelige oppdateringer. Hver linje inneholder:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Programnavn - Det fulle navnet på programmet")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Nåværende versjon - Versjonen som er installert på systemet ditt")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Tilgjengelig versjon - Den nyeste versjonen som er tilgjengelig")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Kilde - Hvor oppdateringen kommer fra (vanligvis winget)")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Skippa - Lenke for å hoppe over denne oppdateringen")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # Note about image placeholder
        p = P(stylename=note_style)
        p.addText("📷 [Bilde: Hovedvinduet med oppdateringsliste]")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 3. Basic usage
        h = H(outlinelevel=1, stylename=h1_style)
        h.addText("3. Grunnleggende bruk")
        doc.text.addElement(h)
        
        # 3.1 Installing updates
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("3.1 Installere oppdateringer")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("For å installere oppdateringer:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("1. Merk av for oppdateringene du ønsker å installere ved å klikke på avmerkingsboksen til venstre for hvert program")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("2. Klikk på \"Oppdater nå\"-knappen nederst i vinduet")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("3. Godkjenn UAC-forespørselen (User Account Control) som dukker opp")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("4. Vent mens oppdateringene installeres - du vil se en fremdriftsindikator")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        p = P(stylename=note_style)
        p.addText("💡 Tips: Du kan bruke \"Velg alle\"-knappen for raskt å velge alle tilgjengelige oppdateringer.")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 3.2 Installation dialog
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("3.2 Installasjonsdialog")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Under installasjon vises en egen dialog med følgende informasjon:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• En animert nedlastningsindikator som viser status:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("  - Blå farge: Forbereder eller installerer")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("  - Grønn farge: Laster ned pakke")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Fremdriftslinje som viser totalt fremdrift (0-100%)")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Statustekst som viser hvilken pakke som behandles")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        p = P(stylename=note_style)
        p.addText("📷 [Bilde: Installasjonsdialog med fremdriftsindikator]")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 3.3 Skipping updates
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("3.3 Hoppe over oppdateringer")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Hvis det er en oppdatering du ikke ønsker å installere akkurat nå, kan du hoppe over den:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("1. Klikk på \"Skippa\"-lenken i høyre kolonne for den aktuelle oppdateringen")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("2. En bekreftelsesdialog vises med informasjon om programmet og versjonen")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("3. Klikk \"Ja\" for å bekrefte at du vil hoppe over denne oppdateringen")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("Den hoppede oppdateringen vil ikke lenger vises i listen før en nyere versjon blir tilgjengelig.")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 3.4 Unskipping updates
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("3.4 Angre hopping over")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Hvis du har hoppet over en oppdatering og vil angre dette:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("1. Klikk på \"Oskippa\"-knappen nederst i vinduet")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("2. En dialog viser alle oppdateringer du har hoppet over")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("3. Merk av for oppdateringene du vil gjenoppta")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("4. Klikk \"OK\" for å bekrefte")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("De valgte oppdateringene vil nå vises igjen i hovedlisten hvis de fortsatt er tilgjengelige.")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 3.5 Refresh
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("3.5 Last inn på nytt")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("For å søke etter nye oppdateringer manuelt, klikk på \"Last inn\"-knappen. Dette starter en ny skanning av alle installerte programmer.")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 4. Configuration
        h = H(outlinelevel=1, stylename=h1_style)
        h.addText("4. Konfigurasjon")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Klikk på \"Konfig\"-knappen for å åpne innstillingsdialogen. Her kan du tilpasse hvordan WinUpdate skal fungere.")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 4.1 Operation modes
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("4.1 Driftsmodus")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("WinUpdate kan kjøre i tre forskjellige moduser:")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        h = H(outlinelevel=3, stylename=h3_style)
        h.addText("4.1.1 Manuell modus (Standard)")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("I manuell modus må du starte WinUpdate selv når du vil søke etter oppdateringer. Programmet legger seg ikke i systemstatusfeltet og starter ikke automatisk.")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        h = H(outlinelevel=3, stylename=h3_style)
        h.addText("4.1.2 Skjult skanning ved oppstart")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("I denne modusen starter WinUpdate automatisk når du logger inn på Windows. Programmet kjører en skanning i bakgrunnen og viser et varsel hvis oppdateringer finnes. Hvis systemet er oppdatert, vises ingen melding.")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Oppstartssnarveien opprettes automatisk med --hidden parameter")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Ingen varsel hvis ingen oppdateringer finnes")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Perfekt for brukere som vil ha automatisk sjekk uten permanent ikon")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        h = H(outlinelevel=3, stylename=h3_style)
        h.addText("4.1.3 Systemstatusfelt med periodisk skanning")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Dette er den mest automatiserte modusen. WinUpdate legger seg i systemstatusfeltet (ved siden av klokken) og søker automatisk etter oppdateringer med jevne mellomrom.")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Alltid tilgjengelig via systemstatusikon")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Automatisk skanning basert på valgt intervall")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Varsling når oppdateringer finnes")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Høyreklikk på ikonet for hurtigmeny")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        p = P(stylename=note_style)
        p.addText("📷 [Bilde: Konfigurasjonsdialog med tre moduser]")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 4.2 Polling interval
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("4.2 Skanningsintervall")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Når systemstatusfeltmodus er aktivert, kan du velge hvor ofte WinUpdate skal søke etter oppdateringer:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Hver 2. time")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Hver 4. time")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Hver 6. time")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Hver 12. time")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• En gang daglig (24 timer)")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("Velg intervallet som passer best for deg. Et kortere intervall holder systemet mer oppdatert, men bruker litt mer systemressurser.")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 4.3 Language selection
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("4.3 Språkvalg")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("WinUpdate støtter tre språk:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Norsk (bokmål) - nb_NO")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Engelsk (britisk) - en_GB")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Svensk - sv_SE")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("Språkendringen trer i kraft umiddelbart når du klikker \"OK\" eller \"Bruk\".")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 4.4 Adding to tray immediately
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("4.4 Legg til i systemstatusfelt nå")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Når du velger systemstatusfeltmodus, vises en ekstra knapp: \"Legg til i systemstatusfelt nå\". Denne knappen lar deg umiddelbart legge programmet i systemstatusfeltet uten å måtte starte det på nytt.")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 5. System Tray Features
        h = H(outlinelevel=1, stylename=h1_style)
        h.addText("5. Systemstatusfelt-funksjoner")
        doc.text.addElement(h)
        
        # 5.1 Tray icon
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("5.1 Statusikon")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Når WinUpdate kjører i systemstatusfeltet, vises et ikon ved siden av klokken. Dette ikonet gir deg rask tilgang til programmets funksjoner.")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        p = P(stylename=note_style)
        p.addText("📷 [Bilde: Systemstatusikon]")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 5.2 Tooltip
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("5.2 Verktøytips")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Hold musepekeren over systemstatusikonet for å se når neste automatiske skanning vil finne sted. Verktøytipset viser:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Programnavn (WinUpdate)")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Tidspunkt for neste skanning (HH:MM)")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("Verktøytipset oppdateres automatisk hvert minutt for å vise korrekt tid.")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 5.3 Context menu
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("5.3 Hurtigmeny")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Høyreklikk på systemstatusikonet for å åpne hurtigmenyen. Her har du følgende alternativer:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• ⚡ Søk nå! - Starter en umiddelbar skanning etter oppdateringer")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• 🪟 Åpne hovedvindu - Viser hovedvinduet med oppdateringslisten")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• ❌ Avslutt - Lukker WinUpdate fullstendig")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        p = P(stylename=note_style)
        p.addText("📷 [Bilde: Hurtigmeny fra systemstatusikonet]")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 5.4 Balloon notifications
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("5.4 Varselmeldinger")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("WinUpdate viser varselmeldinger (ballongvarsler) i følgende situasjoner:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Når automatisk skanning finner tilgjengelige oppdateringer - viser antall oppdateringer")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Når manuell skanning fullføres uten å finne oppdateringer - viser \"Du er oppdatert!\"")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("Automatiske skanninger som ikke finner oppdateringer viser ingen melding for å unngå unødvendige forstyrrelser.")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 5.5 Window behavior
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("5.5 Vinduoppførsel")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Når WinUpdate kjører i systemstatusfeltmodus:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Klikk på X-knappen skjuler vinduet i stedet for å lukke programmet")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Programmet fortsetter å kjøre i bakgrunnen")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Gjenåpne vinduet via hurtigmenyen eller ved å klikke på varslingen")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• For å lukke programmet fullstendig, bruk \"Avslutt\" i hurtigmenyen")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 6. Advanced features
        h = H(outlinelevel=1, stylename=h1_style)
        h.addText("6. Avanserte funksjoner")
        doc.text.addElement(h)
        
        # 6.1 Log files
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("6.1 Loggfiler")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Klikk på \"Logg\"-knappen for å åpne loggmappen. Her finner du detaljerte loggfiler over alle operasjoner WinUpdate har utført:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• wup_run_log.txt - Hovedloggfil med alle hendelser")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Andre diagnostiske filer for feilsøking")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("Loggfilene er nyttige hvis du opplever problemer og trenger å feilsøke.")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 6.2 Settings files
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("6.2 Innstillingsfiler")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("WinUpdate lagrer sine innstillinger i følgende mappe:")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("%APPDATA%\\WinUpdate\\")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("Viktige filer:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• wup_settings.ini - Hovedinnstillingsfil (språk, modus, intervall)")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• wup_exclude.txt - Liste over hoppede oppdateringer")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 6.3 Single instance
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("6.3 Én instans av gangen")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("WinUpdate tillater bare én kjørende instans av programmet. Hvis du prøver å starte programmet mens det allerede kjører:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Det eksisterende vinduet vil bli brakt til forgrunnen")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Ingen ny instans vil bli opprettet")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Dette forhindrer konflikter og doble varsler")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 6.4 Automatic startup verification
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("6.4 Automatisk verifikasjon av oppstartssnarveier")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Hver gang WinUpdate starter, verifiserer programmet at oppstartssnarveien samsvarer med den konfigurerte modusen:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Manuell modus: Fjerner eventuell oppstartssnarveier")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Oppstartsmodus: Sikrer snarvei med --hidden parameter")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Systemstatusfelts-modus: Sikrer snarvei med --systray parameter")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("Hvis det oppdages avvik, korrigeres dette automatisk. Dette sikrer at programmet alltid starter på riktig måte.")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 7. Troubleshooting
        h = H(outlinelevel=1, stylename=h1_style)
        h.addText("7. Feilsøking")
        doc.text.addElement(h)
        
        # 7.1 Windows Defender
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("7.1 Windows Defender falske positiver")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("I noen tilfeller kan Windows Defender feilvarsle om at WinUpdate er skadelig programvare. Dette er en falsk positiv. WinUpdate er helt trygt å bruke.")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("For å løse dette:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("1. Åpne DEFENDER_SOLUTIONS.md-filen i programmappen")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("2. Følg instruksjonene for å legge til et unntak i Windows Defender")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("3. Alternativt kan du kjøre scripts/add_defender_exclusion.ps1 med administratorrettigheter")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("Du kan også rapportere den falske positive til Microsoft ved å bruke scripts/submit_false_positive.ps1.")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 7.2 Winget not installed
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("7.2 Winget er ikke installert")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("WinUpdate krever Windows Package Manager (winget). Hvis du får en feilmelding om at winget ikke er funnet:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("1. Åpne Microsoft Store")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("2. Søk etter \"App Installer\"")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("3. Installer eller oppdater App Installer")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("4. Start WinUpdate på nytt")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 7.3 Installation fails
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("7.3 Installasjon mislykkes")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Hvis en oppdatering ikke installeres:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Sjekk at du har administratorrettigheter")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Lukk programmet som skal oppdateres hvis det kjører")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Sjekk loggfilen for detaljerte feilmeldinger")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Prøv å installere oppdateringen manuelt via kommandolinjen: winget upgrade <pakke-id>")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 7.4 Tray icon not visible
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("7.4 Systemstatusikon vises ikke")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Hvis systemstatusikonet ikke vises i systemstatusfeltet:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Klikk på pilen (^) i systemstatusfeltet for å se skjulte ikoner")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Dra WinUpdate-ikonet til hovedområdet for å gjøre det permanent synlig")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• I Windows-innstillinger: Systemstatusområde → Velg hvilke ikoner som skal vises på oppgavelinjen")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 8. About
        h = H(outlinelevel=1, stylename=h1_style)
        h.addText("8. Om WinUpdate")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Klikk på \"Om\"-knappen i hovedvinduet for å se informasjon om programmet:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Programversjon")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Lisensinformasjon (GPLv2)")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Opphavsrettsinformasjon")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Kontaktinformasjon")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        p = P(stylename=note_style)
        p.addText("📷 [Bilde: Om-dialogen]")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 9. Technical details
        h = H(outlinelevel=1, stylename=h1_style)
        h.addText("9. Tekniske detaljer")
        doc.text.addElement(h)
        
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("9.1 Arkitektur")
        doc.text.addElement(h)
        
        p = P(stylename=list_style)
        p.addText("• Bygget med C++ og Win32 API")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Native Windows-applikasjon (ingen ekstra avhengigheter)")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• CMake byggesystem med MinGW GCC 15.2.0")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Modulær kodestruktur for enkel vedlikehold")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("9.2 Sikkerhet")
        doc.text.addElement(h)
        
        p = P(stylename=list_style)
        p.addText("• ASLR (Address Space Layout Randomization) aktivert")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• DEP (Data Execution Prevention) aktivert")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• High-entropy ASLR for økt sikkerhet")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Trådsikker tilgang til delt tilstand med mutex-beskyttelse")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        h = H(outlinelevel=2, stylename=h2_style)
        h.addText("9.3 Filplasseringer")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("Programfiler:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• WinUpdate.exe - Hovedprogrammet")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• winget_helper.exe - Hjelpeprogrammet for installasjon")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• closetray.exe - Verktøy for å lukke systemstatusinstansen")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• locale/ - Språkfiler (en_GB.txt, nb_NO.txt, sv_SE.txt)")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("Brukerdata (%APPDATA%\\WinUpdate\\):")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• wup_settings.ini - Innstillinger")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• wup_exclude.txt - Hoppede oppdateringer")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• logs/ - Loggfiler")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # 10. License
        h = H(outlinelevel=1, stylename=h1_style)
        h.addText("10. Lisens")
        doc.text.addElement(h)
        
        p = P(stylename=normal_style)
        p.addText("WinUpdate er fri programvare lisensiert under GNU General Public License versjon 2 (GPLv2).")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("Dette betyr at du fritt kan:")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Bruke programmet til ethvert formål")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Studere hvordan programmet fungerer og tilpasse det")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Redistribuere kopier")
        doc.text.addElement(p)
        
        p = P(stylename=list_style)
        p.addText("• Forbedre programmet og dele forbedringene")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("Se GPLv2.md-filen i programmappen for fullstendig lisensinformasjon.")
        doc.text.addElement(p)
        
        doc.text.addElement(P(stylename=normal_style))
        
        # Footer
        doc.text.addElement(P(stylename=normal_style))
        doc.text.addElement(P(stylename=normal_style))
        
        p = P(stylename=normal_style)
        p.addText("───────────────────────────────────────────────")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("WinUpdate - Forenkle Windows-oppdateringer")
        doc.text.addElement(p)
        
        p = P(stylename=normal_style)
        p.addText("© 2025-2026 NalleBerg")
        doc.text.addElement(p)
    
    # Save document
    doc.save(filename)
    print(f"✓ Created {filename}")

if __name__ == "__main__":
    create_manual("WinUpdate_Brukermanual_NO.odt", language='no')
    print("\nManual created successfully!")
    print("You can now:")
    print("1. Open the file in LibreOffice Writer or Microsoft Word")
    print("2. Add images where you see 📷 [Image: ...] placeholders")
    print("3. Format and adjust as needed")
    print("4. Save and share!")
