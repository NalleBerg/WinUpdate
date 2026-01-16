#!/usr/bin/env python3
"""Translate Norwegian manual to UK English and Swedish"""

from odf import opendocument, text, style, draw
from odf.element import Element
import copy

# Translation dictionaries
NO_TO_EN = {
    # Title and header
    "Brukermanual": "User Manual",
    "Versjon": "Version",
    "Sist oppdatert": "Last updated",
    "januar": "January",
    "februar": "February",
    "mars": "March",
    "april": "April",
    "mai": "May",
    "juni": "June",
    "juli": "July",
    "august": "August",
    "september": "September",
    "oktober": "October",
    "november": "November",
    "desember": "December",
    
    # Main sections
    "Innledning": "Introduction",
    "Hovedfunksjoner": "Key Features",
    "Systemkrav": "System Requirements",
    "Komme i gang": "Getting Started",
    "Første oppstart": "First Launch",
    "Hovedvinduet": "Main Window",
    "Grunnleggende bruk": "Basic Usage",
    "Installere oppdateringer": "Installing Updates",
    "Installasjonsdialog": "Installation Dialog",
    "Hoppe over oppdateringer": "Skipping Updates",
    "Angre hopping over": "Unskipping Updates",
    "Last inn på nytt": "Refresh",
    "Konfigurasjon": "Configuration",
    "Driftsmodus": "Operation Mode",
    "Manuell modus (Standard)": "Manual Mode (Default)",
    "Skjult skanning ved oppstart": "Hidden Scan at Startup",
    "Systemstatusfelt med periodisk skanning": "System Tray with Periodic Scanning",
    "Skanningsintervall": "Scanning Interval",
    "Språkvalg": "Language Selection",
    "Legg til i systemstatusfelt nå": "Add to System Tray Now",
    "Systemstatusfelt-funksjoner": "System Tray Features",
    "Statusikon": "Tray Icon",
    "Verktøytips": "Tooltip",
    "Hurtigmeny": "Context Menu",
    "Varselmeldinger": "Balloon Notifications",
    "Vinduoppførsel": "Window Behaviour",
    "Avanserte funksjoner": "Advanced Features",
    "Loggfiler": "Log Files",
    "Innstillingsfiler": "Settings Files",
    "Én instans av gangen": "Single Instance",
    "Automatisk verifikasjon av oppstartssnarveier": "Automatic Startup Shortcut Verification",
    "Feilsøking": "Troubleshooting",
    "Windows Defender falske positiver": "Windows Defender False Positives",
    "Winget er ikke installert": "Winget Not Installed",
    "Installasjon mislykkes": "Installation Fails",
    "Systemstatusikon vises ikke": "Tray Icon Not Visible",
    "Om WinUpdate": "About WinUpdate",
    "Tekniske detaljer": "Technical Details",
    "Arkitektur": "Architecture",
    "Sikkerhet": "Security",
    "Filplasseringer": "File Locations",
    "Lisens": "Licence",
    
    # Content phrases
    "er et moderne Windows-program": "is a modern Windows application",
    "som forenkler administrasjon av programvareoppdateringer": "that simplifies software update management",
    "på din datamaskin": "on your computer",
    "Programmet fungerer som et brukervennlig grensesnitt": "The application provides a user-friendly interface",
    "for Windows Package Manager": "for Windows Package Manager",
    "og gir deg full kontroll over": "and gives you full control over",
    "hvilke programmer som skal oppdateres og når": "which programs to update and when",
    
    "Automatisk søking etter tilgjengelige oppdateringer": "Automatic scanning for available updates",
    "Oversiktlig liste over alle tilgjengelige oppdateringer": "Clear list of all available updates",
    "Mulighet til å hoppe over spesifikke oppdateringer": "Ability to skip specific updates",
    "Systemstatusikon med periodisk skanning": "System tray icon with periodic scanning",
    "Flerspråklig støtte": "Multi-language support",
    "norsk, engelsk, svensk": "Norwegian, English, Swedish",
    "Visuell fremdrifgsindikator under installasjon": "Visual progress indicator during installation",
    
    "eller nyere": "or later",
    "installert": "installed",
    "Administratorrettigheter for installasjon av oppdateringer": "Administrator rights for installing updates",
    
    "Når du starter WinUpdate for første gang": "When you start WinUpdate for the first time",
    "vil programmet automatisk søke etter tilgjengelige oppdateringer": "the application will automatically scan for available updates",
    "Dette kan ta noen sekunder": "This may take a few seconds",
    "avhengig av hvor mange programmer du har installert": "depending on how many programs you have installed",
    
    "viser en oversikt over alle tilgjengelige oppdateringer": "displays an overview of all available updates",
    "Hver linje inneholder": "Each line contains",
    "Programnavn": "Program Name",
    "Det fulle navnet på programmet": "The full name of the program",
    "Nåværende versjon": "Current Version",
    "Versjonen som er installert på systemet ditt": "The version installed on your system",
    "Tilgjengelig versjon": "Available Version",
    "Den nyeste versjonen som er tilgjengelig": "The latest version available",
    "Kilde": "Source",
    "Hvor oppdateringen kommer fra": "Where the update comes from",
    "vanligvis winget": "usually winget",
    "Skippe": "Skip",
    "Lenke for å hoppe over denne oppdateringen": "Link to skip this update",
    
    "[Bilde:": "[Image:",
    "med oppdateringsliste]": "with update list]",
    "med fremdriftsindikator]": "with progress indicator]",
    
    "For å installere oppdateringer": "To install updates",
    "Merk av for oppdateringene du ønsker å installere": "Check the updates you want to install",
    "ved å klikke på avmerkingsboksen til venstre for hvert program": "by clicking the checkbox to the left of each program",
    "Klikk på": "Click the",
    "knappen nederst i vinduet": "button at the bottom of the window",
    "Godkjenn UAC-forespørselen": "Approve the UAC prompt",
    "som dukker opp": "that appears",
    "Vent mens oppdateringene installeres": "Wait while the updates are installed",
    "du vil se en fremdriftsindikator": "you will see a progress indicator",
    
    "Tips:": "Tip:",
    "Du kan bruke": "You can use the",
    "knappen for raskt å velge alle tilgjengelige oppdateringer": "button to quickly select all available updates",
    
    "Under installasjon vises en egen dialog med følgende informasjon": "During installation, a separate dialog displays the following information",
    "En animert nedlastingsindikator som viser status": "An animated download indicator showing status",
    "Blå farge": "Blue colour",
    "Forbereder eller installerer": "Preparing or installing",
    "Grønn farge": "Green colour",
    "Laster ned pakke": "Downloading package",
    "Fremdriftslinje som viser totalt fremdrift": "Progress bar showing total progress",
    "Statustekst som viser hvilken pakke som behandles": "Status text showing which package is being processed",
    
    "Hvis det er en oppdatering du ikke ønsker å installere akkurat nå": "If there is an update you do not want to install right now",
    "kan du hoppe over den": "you can skip it",
    "lenken i høyre kolonne for den aktuelle oppdateringen": "link in the right column for that update",
    "En bekreftelsesdialog vises med informasjon om programmet og versjonen": "A confirmation dialog appears with information about the program and version",
    "for å bekrefte at du vil hoppe over denne oppdateringen": "to confirm that you want to skip this update",
    "Den hoppede oppdateringen vil ikke lenger vises i listen": "The skipped update will no longer appear in the list",
    "før en nyere versjon blir tilgjengelig": "until a newer version becomes available",
    
    "Hvis du har hoppet over en oppdatering og vil angre dette": "If you have skipped an update and want to undo this",
    "En dialog viser alle oppdateringer du har hoppet over": "A dialog shows all updates you have skipped",
    "Merk av for oppdateringene du vil gjenoppta": "Check the updates you want to resume",
    "De valgte oppdateringene vil nå vises igjen i hovedlisten": "The selected updates will now appear again in the main list",
    "hvis de fortsatt er tilgjengelige": "if they are still available",
    
    "For å søke etter nye oppdateringer manuelt": "To search for new updates manually",
    "klikk på": "click the",
    "Dette starter en ny skanning av alle installerte programmer": "This starts a new scan of all installed programs",
    
    "for å åpne innstillingsdialogen": "to open the settings dialog",
    "Her kan du tilpasse hvordan WinUpdate skal fungere": "Here you can customise how WinUpdate should work",
    
    "WinUpdate kan kjøre i tre forskjellige moduser": "WinUpdate can run in three different modes",
    
    "I manuell modus må du starte WinUpdate selv": "In manual mode, you must start WinUpdate yourself",
    "når du vil søke etter oppdateringer": "when you want to search for updates",
    "Programmet legger seg ikke i systemstatusfeltet": "The application does not add itself to the system tray",
    "og starter ikke automatisk": "and does not start automatically",
    
    "I denne modusen starter WinUpdate automatisk": "In this mode, WinUpdate starts automatically",
    "når du logger inn på Windows": "when you log in to Windows",
    "Programmet kjører en skanning i bakgrunnen": "The application runs a scan in the background",
    "og viser et varsel hvis oppdateringer finnes": "and shows a notification if updates are found",
    "Hvis systemet er oppdatert": "If the system is up to date",
    "vises ingen melding": "no message is displayed",
    "Oppstartssnarveien opprettes automatisk med": "The startup shortcut is created automatically with",
    "parameter": "parameter",
    "Ingen varsel hvis ingen oppdateringer finnes": "No notification if no updates are found",
    "Perfekt for brukere som vil ha automatisk sjekk": "Perfect for users who want automatic checking",
    "uten permanent ikon": "without a permanent icon",
    
    "Dette er den mest automatiserte modusen": "This is the most automated mode",
    "legger seg i systemstatusfeltet": "adds itself to the system tray",
    "ved siden av klokken": "next to the clock",
    "og søker automatisk etter oppdateringer med jevne mellomrom": "and automatically searches for updates at regular intervals",
    "Alltid tilgjengelig via systemstatusikon": "Always available via system tray icon",
    "Automatisk skanning basert på valgt intervall": "Automatic scanning based on selected interval",
    "Varsling når oppdateringer finnes": "Notification when updates are found",
    "Høyreklikk på ikonet for hurtigmeny": "Right-click the icon for context menu",
    
    "Konfigurasjonsdialog med tre moduser]": "Configuration dialog with three modes]",
    
    "Når systemstatusfeltmodus er aktivert": "When system tray mode is enabled",
    "kan du velge hvor ofte WinUpdate skal søke etter oppdateringer": "you can choose how often WinUpdate should search for updates",
    "Hver": "Every",
    "time": "hour",
    "timer": "hours",
    "En gang daglig": "Once daily",
    "Velg intervallet som passer best for deg": "Choose the interval that suits you best",
    "Et kortere intervall holder systemet mer oppdatert": "A shorter interval keeps the system more up to date",
    "men bruker litt mer systemressurser": "but uses slightly more system resources",
    
    "støtter tre språk": "supports three languages",
    "Norsk (bokmål)": "Norwegian (Bokmål)",
    "Engelsk (britisk)": "English (British)",
    "Svensk": "Swedish",
    "Språkendringen trer i kraft umiddelbart": "The language change takes effect immediately",
    "når du klikker": "when you click",
    "eller": "or",
    
    "Når du velger systemstatusfeltmodus": "When you select system tray mode",
    "vises en ekstra knapp": "an additional button appears",
    "Denne knappen lar deg umiddelbart legge programmet i systemstatusfeltet": "This button allows you to immediately add the application to the system tray",
    "uten å måtte starte det på nytt": "without having to restart it",
    
    "Når WinUpdate kjører i systemstatusfeltet": "When WinUpdate runs in the system tray",
    "vises et ikon ved siden av klokken": "an icon appears next to the clock",
    "Dette ikonet gir deg rask tilgang til programmets funksjoner": "This icon gives you quick access to the application's features",
    
    "Systemstatusikon]": "System tray icon]",
    
    "Hold musepekeren over systemstatusikonet": "Hover the mouse pointer over the system tray icon",
    "for å se når neste automatiske skanning vil finne sted": "to see when the next automatic scan will take place",
    "Verktøytipset viser": "The tooltip shows",
    "Tidspunkt for neste skanning": "Time of next scan",
    "Verktøytipset oppdateres automatisk hvert minutt": "The tooltip updates automatically every minute",
    "for å vise korrekt tid": "to show the correct time",
    
    "Høyreklikk på systemstatusikonet for å åpne hurtigmenyen": "Right-click the system tray icon to open the context menu",
    "Her har du følgende alternativer": "Here you have the following options",
    "Søk nå!": "Scan Now!",
    "Starter en umiddelbar skanning etter oppdateringer": "Starts an immediate scan for updates",
    "Åpne hovedvindu": "Open Main Window",
    "Viser hovedvinduet med oppdateringslisten": "Shows the main window with the update list",
    "Avslutt": "Exit",
    "Lukker WinUpdate fullstendig": "Closes WinUpdate completely",
    
    "Hurtigmeny fra systemstatusikonet]": "Context menu from system tray icon]",
    
    "viser varselmeldinger": "displays balloon notifications",
    "ballongvarsler": "balloon notifications",
    "i følgende situasjoner": "in the following situations",
    "Når automatisk skanning finner tilgjengelige oppdateringer": "When automatic scanning finds available updates",
    "viser antall oppdateringer": "shows the number of updates",
    "Når manuell skanning fullføres uten å finne oppdateringer": "When manual scanning completes without finding updates",
    "viser": "shows",
    "Du er oppdatert!": "You are up to date!",
    "Automatiske skanninger som ikke finner oppdateringer viser ingen melding": "Automatic scans that find no updates display no message",
    "for å unngå unødvendige forstyrrelser": "to avoid unnecessary disturbances",
    
    "Klikk på X-knappen skjuler vinduet": "Clicking the X button hides the window",
    "i stedet for å lukke programmet": "instead of closing the application",
    "fortsetter å kjøre i bakgrunnen": "continues to run in the background",
    "Gjenåpne vinduet via hurtigmenyen": "Reopen the window via the context menu",
    "eller ved å klikke på varslingen": "or by clicking the notification",
    "For å lukke programmet fullstendig": "To close the application completely",
    "bruk": "use",
    "i hurtigmenyen": "in the context menu",
    
    "for å åpne loggmappen": "to open the log folder",
    "Her finner du detaljerte loggfiler over alle operasjoner": "Here you will find detailed log files of all operations",
    "har utført": "has performed",
    "Hovedloggfil med alle hendelser": "Main log file with all events",
    "Andre diagnostiske filer for feilsøking": "Other diagnostic files for troubleshooting",
    "Loggfilene er nyttige hvis du opplever problemer": "The log files are useful if you experience problems",
    "og trenger å feilsøke": "and need to troubleshoot",
    
    "lagrer sine innstillinger i følgende mappe": "stores its settings in the following folder",
    "Viktige filer": "Important files",
    "Hovedinnstillingsfil": "Main settings file",
    "språk, modus, intervall": "language, mode, interval",
    "Liste over hoppede oppdateringer": "List of skipped updates",
    
    "tillater bare én kjørende instans av programmet": "allows only one running instance of the application",
    "Hvis du prøver å starte programmet mens det allerede kjører": "If you try to start the application while it is already running",
    "Det eksisterende vinduet vil bli brakt til forgrunnen": "The existing window will be brought to the foreground",
    "Ingen ny instans vil bli opprettet": "No new instance will be created",
    "Dette forhindrer konflikter og doble varsler": "This prevents conflicts and duplicate notifications",
    
    "Hver gang WinUpdate starter": "Every time WinUpdate starts",
    "verifiserer programmet at oppstartssnarveien samsvarer": "the application verifies that the startup shortcut matches",
    "med den konfigurerte modusen": "the configured mode",
    "Fjerner eventuell oppstartssnarveier": "Removes any startup shortcuts",
    "Sikrer snarvei med": "Ensures shortcut with",
    "Hvis det oppdages avvik": "If discrepancies are detected",
    "korrigeres dette automatisk": "they are corrected automatically",
    "Dette sikrer at programmet alltid starter på riktig måte": "This ensures the application always starts correctly",
    
    "I noen tilfeller kan Windows Defender feilvarsle": "In some cases, Windows Defender may falsely warn",
    "om at WinUpdate er skadelig programvare": "that WinUpdate is malicious software",
    "Dette er en falsk positiv": "This is a false positive",
    "er helt trygt å bruke": "is completely safe to use",
    "For å løse dette": "To resolve this",
    "Åpne": "Open",
    "filen i programmappen": "file in the program folder",
    "Følg instruksjonene for å legge til et unntak": "Follow the instructions to add an exclusion",
    "i Windows Defender": "in Windows Defender",
    "Alternativt kan du kjøre": "Alternatively, you can run",
    "med administratorrettigheter": "with administrator rights",
    "Du kan også rapportere den falske positive til Microsoft": "You can also report the false positive to Microsoft",
    "ved å bruke": "by using",
    
    "krever Windows Package Manager": "requires Windows Package Manager",
    "Hvis du får en feilmelding om at winget ikke er funnet": "If you get an error message that winget is not found",
    "Søk etter": "Search for",
    "Installer eller oppdater": "Install or update",
    "Start WinUpdate på nytt": "Restart WinUpdate",
    
    "Hvis en oppdatering ikke installeres": "If an update fails to install",
    "Sjekk at du har administratorrettigheter": "Check that you have administrator rights",
    "Lukk programmet som skal oppdateres hvis det kjører": "Close the program to be updated if it is running",
    "Sjekk loggfilen for detaljerte feilmeldinger": "Check the log file for detailed error messages",
    "Prøv å installere oppdateringen manuelt via kommandolinjen": "Try to install the update manually via the command line",
    
    "Hvis systemstatusikonet ikke vises i systemstatusfeltet": "If the system tray icon does not appear in the system tray",
    "Klikk på pilen": "Click the arrow",
    "i systemstatusfeltet for å se skjulte ikoner": "in the system tray to see hidden icons",
    "Dra WinUpdate-ikonet til hovedområdet": "Drag the WinUpdate icon to the main area",
    "for å gjøre det permanent synlig": "to make it permanently visible",
    "I Windows-innstillinger": "In Windows settings",
    "Systemstatusområde": "System tray",
    "Velg hvilke ikoner som skal vises på oppgavelinjen": "Select which icons to show on the taskbar",
    
    "for å se informasjon om programmet": "to see information about the application",
    "Programversjon": "Program version",
    "Lisensinformasjon": "Licence information",
    "Opphavsrettsinformasjon": "Copyright information",
    "Kontaktinformasjon": "Contact information",
    
    "Om-dialogen]": "About dialog]",
    
    "Bygget med C++ og Win32 API": "Built with C++ and Win32 API",
    "Native Windows-applikasjon": "Native Windows application",
    "ingen ekstra avhengigheter": "no additional dependencies",
    "CMake byggesystem med MinGW GCC": "CMake build system with MinGW GCC",
    "Modulær kodestruktur for enkel vedlikehold": "Modular code structure for easy maintenance",
    
    "aktivert": "enabled",
    "for økt sikkerhet": "for increased security",
    "Trådsikker tilgang til delt tilstand": "Thread-safe access to shared state",
    "med mutex-beskyttelse": "with mutex protection",
    
    "Programfiler": "Program files",
    "Hovedprogrammet": "Main program",
    "Hjelpeprogrammet for installasjon": "Installation helper program",
    "Verktøy for å lukke systemstatusinstansen": "Tool to close the system tray instance",
    "Språkfiler": "Language files",
    "Brukerdata": "User data",
    "Innstillinger": "Settings",
    "Hoppede oppdateringer": "Skipped updates",
    
    "er fri programvare lisensiert under": "is free software licenced under",
    "Dette betyr at du fritt kan": "This means you are free to",
    "Bruke programmet til ethvert formål": "Use the program for any purpose",
    "Studere hvordan programmet fungerer og tilpasse det": "Study how the program works and adapt it",
    "Redistribuere kopier": "Redistribute copies",
    "Forbedre programmet og dele forbedringene": "Improve the program and share the improvements",
    "Se": "See",
    "filen i programmappen for fullstendig lisensinformasjon": "file in the program folder for full licence information",
    
    "Forenkle Windows-oppdateringer": "Simplify Windows Updates",
}

NO_TO_SV = {
    # Title and header
    "Brukermanual": "Användarmanual",
    "Versjon": "Version",
    "Sist oppdatert": "Senast uppdaterad",
    "januar": "januari",
    "februar": "februari",
    "mars": "mars",
    "april": "april",
    "mai": "maj",
    "juni": "juni",
    "juli": "juli",
    "august": "augusti",
    "september": "september",
    "oktober": "oktober",
    "november": "november",
    "desember": "december",
    
    # Main sections
    "Innledning": "Inledning",
    "Hovedfunksjoner": "Huvudfunktioner",
    "Systemkrav": "Systemkrav",
    "Komme i gang": "Kom igång",
    "Første oppstart": "Första start",
    "Hovedvinduet": "Huvudfönstret",
    "Grunnleggende bruk": "Grundläggande användning",
    "Installere oppdateringer": "Installera uppdateringar",
    "Installasjonsdialog": "Installationsdialog",
    "Hoppe over oppdateringer": "Skippa uppdateringar",
    "Angre hopping over": "Oskippa uppdateringar",
    "Last inn på nytt": "Ladda om",
    "Konfigurasjon": "Konfiguration",
    "Driftsmodus": "Driftläge",
    "Manuell modus (Standard)": "Manuellt läge (Standard)",
    "Skjult skanning ved oppstart": "Dold skanning vid start",
    "Systemstatusfelt med periodisk skanning": "Systemfält med periodisk skanning",
    "Skanningsintervall": "Skanningsintervall",
    "Språkvalg": "Språkval",
    "Legg til i systemstatusfelt nå": "Lägg till i systemfält nu",
    "Systemstatusfelt-funksjoner": "Systemfältfunktioner",
    "Statusikon": "Statusikon",
    "Verktøytips": "Verktygstips",
    "Hurtigmeny": "Snabbmeny",
    "Varselmeldinger": "Aviseringar",
    "Vinduoppførsel": "Fönsterbeteende",
    "Avanserte funksjoner": "Avancerade funktioner",
    "Loggfiler": "Loggfiler",
    "Innstillingsfiler": "Inställningsfiler",
    "Én instans av gangen": "En instans åt gången",
    "Automatisk verifikasjon av oppstartssnarveier": "Automatisk verifiering av startgenvägar",
    "Feilsøking": "Felsökning",
    "Windows Defender falske positiver": "Windows Defender falska positiva",
    "Winget er ikke installert": "Winget är inte installerat",
    "Installasjon mislykkes": "Installationen misslyckas",
    "Systemstatusikon vises ikke": "Systemikonen visas inte",
    "Om WinUpdate": "Om WinUpdate",
    "Tekniske detaljer": "Tekniska detaljer",
    "Arkitektur": "Arkitektur",
    "Sikkerhet": "Säkerhet",
    "Filplasseringer": "Filplatser",
    "Lisens": "Licens",
    
    # Content phrases
    "er et moderne Windows-program": "är ett modernt Windows-program",
    "som forenkler administrasjon av programvareoppdateringer": "som förenklar hanteringen av programuppdateringar",
    "på din datamaskin": "på din dator",
    "Programmet fungerer som et brukervennlig grensesnitt": "Programmet fungerar som ett användarvänligt gränssnitt",
    "for Windows Package Manager": "för Windows Package Manager",
    "og gir deg full kontroll over": "och ger dig full kontroll över",
    "hvilke programmer som skal oppdateres og når": "vilka program som ska uppdateras och när",
    
    "Automatisk søking etter tilgjengelige oppdateringer": "Automatisk sökning efter tillgängliga uppdateringar",
    "Oversiktlig liste over alle tilgjengelige oppdateringer": "Tydlig lista över alla tillgängliga uppdateringar",
    "Mulighet til å hoppe over spesifikke oppdateringer": "Möjlighet att skippa specifika uppdateringar",
    "Systemstatusikon med periodisk skanning": "Systemikon med periodisk skanning",
    "Flerspråklig støtte": "Flerspråkigt stöd",
    "norsk, engelsk, svensk": "norska, engelska, svenska",
    "Visuell fremdrifgsindikator under installasjon": "Visuell förloppsindikator under installation",
    
    "eller nyere": "eller senare",
    "installert": "installerat",
    "Administratorrettigheter for installasjon av oppdateringer": "Administratörsrättigheter för installation av uppdateringar",
    
    "Når du starter WinUpdate for første gang": "När du startar WinUpdate för första gången",
    "vil programmet automatisk søke etter tilgjengelige oppdateringer": "kommer programmet automatiskt att söka efter tillgängliga uppdateringar",
    "Dette kan ta noen sekunder": "Detta kan ta några sekunder",
    "avhengig av hvor mange programmer du har installert": "beroende på hur många program du har installerat",
    
    "viser en oversikt over alle tilgjengelige oppdateringer": "visar en översikt över alla tillgängliga uppdateringar",
    "Hver linje inneholder": "Varje rad innehåller",
    "Programnavn": "Programnamn",
    "Det fulle navnet på programmet": "Det fullständiga namnet på programmet",
    "Nåværende versjon": "Nuvarande version",
    "Versjonen som er installert på systemet ditt": "Versionen som är installerad på ditt system",
    "Tilgjengelig versjon": "Tillgänglig version",
    "Den nyeste versjonen som er tilgjengelig": "Den senaste versionen som är tillgänglig",
    "Kilde": "Källa",
    "Hvor oppdateringen kommer fra": "Varifrån uppdateringen kommer",
    "vanligvis winget": "vanligtvis winget",
    "Skippe": "Skippa",
    "Lenke for å hoppe over denne oppdateringen": "Länk för att skippa denna uppdatering",
    
    "[Bilde:": "[Bild:",
    "med oppdateringsliste]": "med uppdateringslista]",
    "med fremdriftsindikator]": "med förloppsindikator]",
    
    "For å installere oppdateringer": "För att installera uppdateringar",
    "Merk av for oppdateringene du ønsker å installere": "Markera uppdateringarna du vill installera",
    "ved å klikke på avmerkingsboksen til venstre for hvert program": "genom att klicka på kryssrutan till vänster om varje program",
    "Klikk på": "Klicka på",
    "knappen nederst i vinduet": "knappen längst ner i fönstret",
    "Godkjenn UAC-forespørselen": "Godkänn UAC-begäran",
    "som dukker opp": "som dyker upp",
    "Vent mens oppdateringene installeres": "Vänta medan uppdateringarna installeras",
    "du vil se en fremdriftsindikator": "du kommer att se en förloppsindikator",
    
    "Tips:": "Tips:",
    "Du kan bruke": "Du kan använda",
    "knappen for raskt å velge alle tilgjengelige oppdateringer": "knappen för att snabbt välja alla tillgängliga uppdateringar",
    
    "Under installasjon vises en egen dialog med følgende informasjon": "Under installationen visas en egen dialog med följande information",
    "En animert nedlastingsindikator som viser status": "En animerad nedladdningsindikator som visar status",
    "Blå farge": "Blå färg",
    "Forbereder eller installerer": "Förbereder eller installerar",
    "Grønn farge": "Grön färg",
    "Laster ned pakke": "Laddar ner paket",
    "Fremdriftslinje som viser totalt fremdrift": "Förloppslinje som visar totalt framsteg",
    "Statustekst som viser hvilken pakke som behandles": "Statustext som visar vilket paket som behandlas",
    
    "Hvis det er en oppdatering du ikke ønsker å installere akkurat nå": "Om det finns en uppdatering du inte vill installera just nu",
    "kan du hoppe over den": "kan du skippa den",
    "lenken i høyre kolonne for den aktuelle oppdateringen": "länken i högra kolumnen för den aktuella uppdateringen",
    "En bekreftelsesdialog vises med informasjon om programmet og versjonen": "En bekräftelsedialog visas med information om programmet och versionen",
    "for å bekrefte at du vil hoppe over denne oppdateringen": "för att bekräfta att du vill skippa denna uppdatering",
    "Den hoppede oppdateringen vil ikke lenger vises i listen": "Den skippade uppdateringen kommer inte längre att visas i listan",
    "før en nyere versjon blir tilgjengelig": "förrän en nyare version blir tillgänglig",
    
    "Hvis du har hoppet over en oppdatering og vil angre dette": "Om du har skippat en uppdatering och vill ångra detta",
    "En dialog viser alle oppdateringer du har hoppet over": "En dialog visar alla uppdateringar du har skippat",
    "Merk av for oppdateringene du vil gjenoppta": "Markera uppdateringarna du vill återuppta",
    "De valgte oppdateringene vil nå vises igjen i hovedlisten": "De valda uppdateringarna kommer nu att visas igen i huvudlistan",
    "hvis de fortsatt er tilgjengelige": "om de fortfarande är tillgängliga",
    
    "For å søke etter nye oppdateringer manuelt": "För att söka efter nya uppdateringar manuellt",
    "klikk på": "klicka på",
    "Dette starter en ny skanning av alle installerte programmer": "Detta startar en ny skanning av alla installerade program",
    
    "for å åpne innstillingsdialogen": "för att öppna inställningsdialogen",
    "Her kan du tilpasse hvordan WinUpdate skal fungere": "Här kan du anpassa hur WinUpdate ska fungera",
    
    "WinUpdate kan kjøre i tre forskjellige moduser": "WinUpdate kan köras i tre olika lägen",
    
    "I manuell modus må du starte WinUpdate selv": "I manuellt läge måste du starta WinUpdate själv",
    "når du vil søke etter oppdateringer": "när du vill söka efter uppdateringar",
    "Programmet legger seg ikke i systemstatusfeltet": "Programmet lägger sig inte i systemfältet",
    "og starter ikke automatisk": "och startar inte automatiskt",
    
    "I denne modusen starter WinUpdate automatisk": "I detta läge startar WinUpdate automatiskt",
    "når du logger inn på Windows": "när du loggar in på Windows",
    "Programmet kjører en skanning i bakgrunnen": "Programmet kör en skanning i bakgrunden",
    "og viser et varsel hvis oppdateringer finnes": "och visar ett meddelande om uppdateringar finns",
    "Hvis systemet er oppdatert": "Om systemet är uppdaterat",
    "vises ingen melding": "visas inget meddelande",
    "Oppstartssnarveien opprettes automatisk med": "Startgenvägen skapas automatiskt med",
    "parameter": "parameter",
    "Ingen varsel hvis ingen oppdateringer finnes": "Ingen avisering om inga uppdateringar finns",
    "Perfekt for brukere som vil ha automatisk sjekk": "Perfekt för användare som vill ha automatisk kontroll",
    "uten permanent ikon": "utan permanent ikon",
    
    "Dette er den mest automatiserte modusen": "Detta är det mest automatiserade läget",
    "legger seg i systemstatusfeltet": "lägger sig i systemfältet",
    "ved siden av klokken": "bredvid klockan",
    "og søker automatisk etter oppdateringer med jevne mellomrom": "och söker automatiskt efter uppdateringar med jämna mellanrum",
    "Alltid tilgjengelig via systemstatusikon": "Alltid tillgänglig via systemikon",
    "Automatisk skanning basert på valgt intervall": "Automatisk skanning baserad på valt intervall",
    "Varsling når oppdateringer finnes": "Avisering när uppdateringar finns",
    "Høyreklikk på ikonet for hurtigmeny": "Högerklicka på ikonen för snabbmeny",
    
    "Konfigurasjonsdialog med tre moduser]": "Konfigurationsdialog med tre lägen]",
    
    "Når systemstatusfeltmodus er aktivert": "När systemfältläget är aktiverat",
    "kan du velge hvor ofte WinUpdate skal søke etter oppdateringer": "kan du välja hur ofta WinUpdate ska söka efter uppdateringar",
    "Hver": "Varje",
    "time": "timme",
    "timer": "timmar",
    "En gang daglig": "En gång dagligen",
    "Velg intervallet som passer best for deg": "Välj intervallet som passar dig bäst",
    "Et kortere intervall holder systemet mer oppdatert": "Ett kortare intervall håller systemet mer uppdaterat",
    "men bruker litt mer systemressurser": "men använder lite mer systemresurser",
    
    "støtter tre språk": "stöder tre språk",
    "Norsk (bokmål)": "Norska (bokmål)",
    "Engelsk (britisk)": "Engelska (brittisk)",
    "Svensk": "Svenska",
    "Språkendringen trer i kraft umiddelbart": "Språkändringen träder i kraft omedelbart",
    "når du klikker": "när du klickar",
    "eller": "eller",
    
    "Når du velger systemstatusfeltmodus": "När du väljer systemfältläge",
    "vises en ekstra knapp": "visas en extra knapp",
    "Denne knappen lar deg umiddelbart legge programmet i systemstatusfeltet": "Denna knapp låter dig omedelbart lägga programmet i systemfältet",
    "uten å måtte starte det på nytt": "utan att behöva starta om det",
    
    "Når WinUpdate kjører i systemstatusfeltet": "När WinUpdate körs i systemfältet",
    "vises et ikon ved siden av klokken": "visas en ikon bredvid klockan",
    "Dette ikonet gir deg rask tilgang til programmets funksjoner": "Denna ikon ger dig snabb tillgång till programmets funktioner",
    
    "Systemstatusikon]": "Systemikon]",
    
    "Hold musepekeren over systemstatusikonet": "Håll muspekaren över systemikonen",
    "for å se når neste automatiske skanning vil finne sted": "för att se när nästa automatiska skanning kommer att äga rum",
    "Verktøytipset viser": "Verktygstipset visar",
    "Tidspunkt for neste skanning": "Tidpunkt för nästa skanning",
    "Verktøytipset oppdateres automatisk hvert minutt": "Verktygstipset uppdateras automatiskt varje minut",
    "for å vise korrekt tid": "för att visa korrekt tid",
    
    "Høyreklikk på systemstatusikonet for å åpne hurtigmenyen": "Högerklicka på systemikonen för att öppna snabbmenyn",
    "Her har du følgende alternativer": "Här har du följande alternativ",
    "Søk nå!": "Sök nu!",
    "Starter en umiddelbar skanning etter oppdateringer": "Startar en omedelbar skanning efter uppdateringar",
    "Åpne hovedvindu": "Öppna huvudfönster",
    "Viser hovedvinduet med oppdateringslisten": "Visar huvudfönstret med uppdateringslistan",
    "Avslutt": "Avsluta",
    "Lukker WinUpdate fullstendig": "Stänger WinUpdate helt",
    
    "Hurtigmeny fra systemstatusikonet]": "Snabbmeny från systemikonen]",
    
    "viser varselmeldinger": "visar aviseringar",
    "ballongvarsler": "ballongaviseringar",
    "i følgende situasjoner": "i följande situationer",
    "Når automatisk skanning finner tilgjengelige oppdateringer": "När automatisk skanning hittar tillgängliga uppdateringar",
    "viser antall oppdateringer": "visar antalet uppdateringar",
    "Når manuell skanning fullføres uten å finne oppdateringer": "När manuell skanning slutförs utan att hitta uppdateringar",
    "viser": "visar",
    "Du er oppdatert!": "Du är uppdaterad!",
    "Automatiske skanninger som ikke finner oppdateringer viser ingen melding": "Automatiska skanningar som inte hittar uppdateringar visar inget meddelande",
    "for å unngå unødvendige forstyrrelser": "för att undvika onödiga störningar",
    
    "Klikk på X-knappen skjuler vinduet": "Att klicka på X-knappen döljer fönstret",
    "i stedet for å lukke programmet": "istället för att stänga programmet",
    "fortsetter å kjøre i bakgrunnen": "fortsätter att köra i bakgrunden",
    "Gjenåpne vinduet via hurtigmenyen": "Öppna fönstret igen via snabbmenyn",
    "eller ved å klikke på varslingen": "eller genom att klicka på aviseringen",
    "For å lukke programmet fullstendig": "För att stänga programmet helt",
    "bruk": "använd",
    "i hurtigmenyen": "i snabbmenyn",
    
    "for å åpne loggmappen": "för att öppna loggmappen",
    "Her finner du detaljerte loggfiler over alle operasjoner": "Här hittar du detaljerade loggfiler över alla operationer",
    "har utført": "har utfört",
    "Hovedloggfil med alle hendelser": "Huvudloggfil med alla händelser",
    "Andre diagnostiske filer for feilsøking": "Andra diagnostiska filer för felsökning",
    "Loggfilene er nyttige hvis du opplever problemer": "Loggfilerna är användbara om du upplever problem",
    "og trenger å feilsøke": "och behöver felsöka",
    
    "lagrer sine innstillinger i følgende mappe": "lagrar sina inställningar i följande mapp",
    "Viktige filer": "Viktiga filer",
    "Hovedinnstillingsfil": "Huvudinställningsfil",
    "språk, modus, intervall": "språk, läge, intervall",
    "Liste over hoppede oppdateringer": "Lista över skippade uppdateringar",
    
    "tillater bare én kjørende instans av programmet": "tillåter bara en körande instans av programmet",
    "Hvis du prøver å starte programmet mens det allerede kjører": "Om du försöker starta programmet medan det redan körs",
    "Det eksisterende vinduet vil bli brakt til forgrunnen": "Det befintliga fönstret kommer att föras till förgrunden",
    "Ingen ny instans vil bli opprettet": "Ingen ny instans kommer att skapas",
    "Dette forhindrer konflikter og doble varsler": "Detta förhindrar konflikter och dubbla aviseringar",
    
    "Hver gang WinUpdate starter": "Varje gång WinUpdate startar",
    "verifiserer programmet at oppstartssnarveien samsvarer": "verifierar programmet att startgenvägen överensstämmer",
    "med den konfigurerte modusen": "med det konfigurerade läget",
    "Fjerner eventuell oppstartssnarveier": "Tar bort eventuella startgenvägar",
    "Sikrer snarvei med": "Säkerställer genväg med",
    "Hvis det oppdages avvik": "Om avvikelser upptäcks",
    "korrigeres dette automatisk": "korrigeras detta automatiskt",
    "Dette sikrer at programmet alltid starter på riktig måte": "Detta säkerställer att programmet alltid startar korrekt",
    
    "I noen tilfeller kan Windows Defender feilvarsle": "I vissa fall kan Windows Defender felaktigt varna",
    "om at WinUpdate er skadelig programvare": "om att WinUpdate är skadlig programvara",
    "Dette er en falsk positiv": "Detta är en falsk positiv",
    "er helt trygt å bruke": "är helt säkert att använda",
    "For å løse dette": "För att lösa detta",
    "Åpne": "Öppna",
    "filen i programmappen": "filen i programmappen",
    "Følg instruksjonene for å legge til et unntak": "Följ instruktionerna för att lägga till ett undantag",
    "i Windows Defender": "i Windows Defender",
    "Alternativt kan du kjøre": "Alternativt kan du köra",
    "med administratorrettigheter": "med administratörsrättigheter",
    "Du kan også rapportere den falske positive til Microsoft": "Du kan också rapportera den falska positiva till Microsoft",
    "ved å bruke": "genom att använda",
    
    "krever Windows Package Manager": "kräver Windows Package Manager",
    "Hvis du får en feilmelding om at winget ikke er funnet": "Om du får ett felmeddelande om att winget inte hittades",
    "Søk etter": "Sök efter",
    "Installer eller oppdater": "Installera eller uppdatera",
    "Start WinUpdate på nytt": "Starta om WinUpdate",
    
    "Hvis en oppdatering ikke installeres": "Om en uppdatering inte installeras",
    "Sjekk at du har administratorrettigheter": "Kontrollera att du har administratörsrättigheter",
    "Lukk programmet som skal oppdateres hvis det kjører": "Stäng programmet som ska uppdateras om det körs",
    "Sjekk loggfilen for detaljerte feilmeldinger": "Kontrollera loggfilen för detaljerade felmeddelanden",
    "Prøv å installere oppdateringen manuelt via kommandolinjen": "Försök att installera uppdateringen manuellt via kommandoraden",
    
    "Hvis systemstatusikonet ikke vises i systemstatusfeltet": "Om systemikonen inte visas i systemfältet",
    "Klikk på pilen": "Klicka på pilen",
    "i systemstatusfeltet for å se skjulte ikoner": "i systemfältet för att se dolda ikoner",
    "Dra WinUpdate-ikonet til hovedområdet": "Dra WinUpdate-ikonen till huvudområdet",
    "for å gjøre det permanent synlig": "för att göra den permanent synlig",
    "I Windows-innstillinger": "I Windows-inställningar",
    "Systemstatusområde": "Systemfält",
    "Velg hvilke ikoner som skal vises på oppgavelinjen": "Välj vilka ikoner som ska visas i aktivitetsfältet",
    
    "for å se informasjon om programmet": "för att se information om programmet",
    "Programversjon": "Programversion",
    "Lisensinformasjon": "Licensinformation",
    "Opphavsrettsinformasjon": "Upphovsrättsinformation",
    "Kontaktinformasjon": "Kontaktinformation",
    
    "Om-dialogen]": "Om-dialogen]",
    
    "Bygget med C++ og Win32 API": "Byggd med C++ och Win32 API",
    "Native Windows-applikasjon": "Native Windows-applikation",
    "ingen ekstra avhengigheter": "inga extra beroenden",
    "CMake byggesystem med MinGW GCC": "CMake byggsystem med MinGW GCC",
    "Modulær kodestruktur for enkel vedlikehold": "Modulär kodstruktur för enkel underhåll",
    
    "aktivert": "aktiverad",
    "for økt sikkerhet": "för ökad säkerhet",
    "Trådsikker tilgang til delt tilstand": "Trådsäker åtkomst till delat tillstånd",
    "med mutex-beskyttelse": "med mutex-skydd",
    
    "Programfiler": "Programfiler",
    "Hovedprogrammet": "Huvudprogrammet",
    "Hjelpeprogrammet for installasjon": "Hjälpprogrammet för installation",
    "Verktøy for å lukke systemstatusinstansen": "Verktyg för att stänga systeminstansen",
    "Språkfiler": "Språkfiler",
    "Brukerdata": "Användardata",
    "Innstillinger": "Inställningar",
    "Hoppede oppdateringer": "Skippade uppdateringar",
    
    "er fri programvare lisensiert under": "är fri programvara licensierad under",
    "Dette betyr at du fritt kan": "Detta betyder att du fritt kan",
    "Bruke programmet til ethvert formål": "Använda programmet för vilket syfte som helst",
    "Studere hvordan programmet fungerer og tilpasse det": "Studera hur programmet fungerar och anpassa det",
    "Redistribuere kopier": "Omfördela kopior",
    "Forbedre programmet og dele forbedringene": "Förbättra programmet och dela förbättringarna",
    "Se": "Se",
    "filen i programmappen for fullstendig lisensinformasjon": "filen i programmappen för fullständig licensinformation",
    
    "Forenkle Windows-oppdateringer": "Förenkla Windows-uppdateringar",
}

def translate_text(text, translation_dict):
    """Translate text using the provided translation dictionary"""
    # Sort by length (longest first) to avoid partial matches
    sorted_keys = sorted(translation_dict.keys(), key=len, reverse=True)
    
    result = text
    for norwegian in sorted_keys:
        english = translation_dict[norwegian]
        result = result.replace(norwegian, english)
    
    return result

def translate_element(element, translation_dict):
    """Recursively translate text content in an element"""
    # Translate text data
    if hasattr(element, 'data') and element.data:
        element.data = translate_text(element.data, translation_dict)
    
    # Recursively translate child elements
    if hasattr(element, 'childNodes'):
        for child in element.childNodes:
            translate_element(child, translation_dict)

def translate_odt(source_file, target_file, translation_dict, language_name):
    """Translate an ODT file using the provided translation dictionary"""
    print(f"Loading {source_file}...")
    source_doc = opendocument.load(source_file)
    
    print(f"Creating {language_name} translation...")
    
    # Translate in-place
    translate_element(source_doc.text, translation_dict)
    
    # Save translated document
    source_doc.save(target_file)
    print(f"✓ Created {target_file}")

if __name__ == "__main__":
    # Translate to UK English
    translate_odt(
        "WinUpdate_Brukermanual_NO.odt",
        "WinUpdate_User_Manual_UK.odt",
        NO_TO_EN,
        "UK English"
    )
    
    # Translate to Swedish
    translate_odt(
        "WinUpdate_Brukermanual_NO.odt",
        "WinUpdate_Användarmanual_SE.odt",
        NO_TO_SV,
        "Swedish"
    )
    
    print("\n✓ All translations completed successfully!")
    print("\nCreated files:")
    print("  - WinUpdate_User_Manual_UK.odt")
    print("  - WinUpdate_Användarmanual_SE.odt")
