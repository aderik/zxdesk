# lf-1220: volledige MSX-verificatie

## Status op 26 september 2026

**Acceptatie staat open.** In deze implementatiestap is de volledige
`msx/test-all.sh` niet uitgevoerd. De instructie voor deze stap zegt expliciet:
“Do not run the full suite here: at most the tests you touched”. Dat beperkt
deze stap; het laat de vereiste volledige run door de tester niet vervallen.

De checkout staat op `abe4286636e6bbfc1e63caf78d33a16cf76ef64a`, de merge
van lf-1219. Dit is de vastgestelde uitgangscommit, **geen geteste commit**.
Deze verslagwijziging verandert geen assembly, testcode of geheugenbudget:
0 extra ROM-, statische RAM- of heapbytes.

| Configuratie | Geslaagd/totaal | Status |
|---|---|---|
| C-BIOS_MSX1_EU (50 Hz) | onbekend | Niet uitgevoerd |
| C-BIOS_MSX1_JP (60 Hz) | onbekend | Niet uitgevoerd |
| Roms_MSX1 | onbekend | Niet uitgevoerd |
| Roms_MSX1 + Roms_Disk | onbekend | Niet uitgevoerd |
| Roms_MSX2 | onbekend | Niet uitgevoerd |

## Nog uit te voeren door de tester

Voer in de testomgeving op de te mergen commit eenmaal de volledige
`msx/test-all.sh` uit, in de voorgrond. Leg vooraf `git rev-parse HEAD` vast
en bewaar stdout én stderr: `msx/test.sh` schrijft de assertions naar stderr.
Gebruik de door de pipeline aangemaakte lokale `msx/roms`-symlink; commit
deze niet. Alle vijf configuraties moeten daadwerkelijk starten: een
overgeslagen ROM-configuratie telt niet als groen.

Vermeld de geteste hash, geslaagd/totaal per configuratie en de logverwijzing
in de testcomment of PR. Tel de assertionregels met eindstatus `ok` of `FAIL`
per configuratie; neem geen aantallen van lf-1217 of gerichte runs over.
Een afgebroken configuratie krijgt de status onvolledig, ook als alle
assertions vóór de afbreking slaagden.

Het huidige `test-all.sh` stopt bij de eerste fout. Voer bij een fout ook
de resterende configuraties uit met `msx/test.sh` en hun `MSX_MACHINE` en
eventuele `MSX_EXT=Roms_Disk`, zodat geen configuratie ongemerkt ontbreekt.
Repareer kleine fouten binnen Notepad/Commander en verifieer de reparatie;
maak voor overige falende subjects een bugticket met hash, configuratie,
subject, reproductie en foutuitvoer. Er zijn in deze stap geen gemeten
testfouten en dus geen bugtickets op basis van een testresultaat.
