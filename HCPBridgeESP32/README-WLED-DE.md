# HCP Bridge mit WLED und Ablaufeditor - Version 2.0.2

Diese Erweiterung basiert auf dem bereitgestellten ZIP von HCPBridgeMqtt (Gifford47). Das PlatformIO-Projekt liegt in diesem Ordner. Die ursprüngliche Lizenz und die Hardware-/Busimplementierung sind enthalten.

## Start in PlatformIO

1. Diesen Ordner mit `platformio.ini` in VS Code / PlatformIO öffnen.
2. Passende Umgebung wählen: `esp32`, `HCP_Giffordv1`, `HCP_Giffordv2` (ESP32-S3), `HCP_Giffordv3`, `m5stack-stamps3` oder `m5stack-atom-lite`.
3. Build und Upload ausführen. Beispiel: `pio run -e HCP_Giffordv2 -t upload`.
4. Bridge im Browser öffnen. Oben links öffnet das Drei-Balken-Menü alle Konfigurationen.

Die Weboberflächen werden beim Build in die Firmware eingebettet. Ein Dateisystem-Upload oder Internetzugriff des Browsers ist nicht erforderlich. Die Bibliotheken werden beim ersten Build von PlatformIO geladen. `ensure_json.py` stellt sicher, dass die optionale ArduinoJson-Unterstützung des Webservers auch in der Bibliothek aktiviert wird.

## Gemeinsames Konfigurationsmenü

- WLAN, MQTT und Gerät
- Sensoren
- Ein- und Ausgänge
- Experteneinstellungen
- WLED-Verbindung: IPv4-Adresse und Aktivierung der Ablaufsteuerung
- Ablaufeditor
- System und Firmware

Die Bridge-Einstellungen verwenden weiterhin ihre ursprünglichen Speichern-/Neustart-Schaltflächen. WLED-Verbindung und Ablauf werden gemeinsam über **Speichern** im Ablaufeditor übernommen, ohne Neustart. Das vorhandene Web-Passwort schützt auch die neuen Seiten und Endpunkte.

## Einen Ablauf erstellen

Unter WLED-Verbindung die IPv4-Adresse des separaten WLED-ESPs eintragen und „Aktiviert“ setzen. Danach das Menü schließen. Blöcke aus der linken Palette auf die Fläche ziehen; alternativ fügt ein Klick einen Block ein. Blöcke lassen sich auf der Fläche verschieben. Ein Klick auf den Block öffnet rechts seine Einstellungen.

Zum Verbinden zuerst den rechten Ausgang, dann den linken Eingang anklicken. Bei einer Bedingung bedeutet der obere Ausgang **Ja**, der untere **Nein**. Ein Klick auf eine Leitung löscht sie. Escape beendet eine begonnene Verbindung. Verzweigungen sind erlaubt; mehrere eingehende Leitungen lösen den Folgeblock jeweils einzeln aus (kein Zusammenführen mit Wartefunktion).

„Beispiel laden“ erzeugt:

- Tor öffnet → WLED Preset 1.
- Tor geschlossen → 3000 ms warten → WLED Preset 2.

Preset 1 und 2 müssen bereits auf WLED gespeichert sein. Das Beispiel vor dem Speichern an die eigenen Presets anpassen. „Manueller Trigger“ startet ausschließlich gespeicherte Blöcke mit dem Auslöser „Manuell“; es simuliert keine Torbewegung.

## Verfügbare Blöcke

| Block | Funktion |
| --- | --- |
| Trigger | Torstatuswechsel, Torlicht an/aus, Bridge-Start, Zeitintervall oder manueller Start |
| Bridge-Status holen | Torstatus, Position, Licht, konfigurierte Sensor- und I/O-Werte in `bridge` ablegen |
| WLED-Status holen | `GET /json/state`, Antwort unter `wled` ablegen |
| Bedingung | Feldpfad mit Wert vergleichen; gleich, ungleich, größer, kleiner |
| Warten | Fortsetzung nach einstellbarer Zeit; blockiert die Torsteuerung nicht |
| WLED Preset | `GET http://<WLED-IP>/win&PL=<1–250>` |
| WLED HTTP-Befehl | Parameter wie `T=1&A=128` an `/win&` senden |
| WLED JSON schreiben | JSON-Objekt mit `POST /json/state` senden |

HTTP-Beispiele nach der offiziellen WLED-Dokumentation:

- `PL=3` aktiviert Preset 3. `PS` würde ein Preset speichern und ist daher kein Preset-Aufruf.
- `T=1&A=128` schaltet ein und setzt die Helligkeit auf 128.
- `T=0` schaltet aus.
- `FX=0&R=255&G=0&B=0` wählt einen statischen roten Effekt.

Der HTTP-Block erwartet nur Parameter, ohne IP, `/win` oder führendes `&`. Für JSON kann beispielsweise `{"on":true,"bri":128}` eingetragen werden.

Bedingungen benötigen vorher einen Statusblock in derselben Kette. Beispiele: `bridge.doorstate` gleich `open`, `bridge.currentPosition` größer als `50`, `bridge.light` gleich `true`, `wled.bri` kleiner als `100`, `wled.ps` gleich `3`. Die tatsächlichen Sensor-/I/O-Feldnamen entsprechen der bestehenden Bridge-Statusschnittstelle. Fehlende Felder ergeben Nein. Vergleiche größer/kleiner sind für numerische Statuswerte vorgesehen. Daten bleiben pro Zweig erhalten; parallele Zweige teilen keine veränderlichen Statusdaten.

## Ausführung und Grenzen

Abläufe und IP werden im nichtflüchtigen ESP32-Speicher abgelegt und bleiben nach Neustarts erhalten. Ein Werksreset löscht auch die WLED-Abläufe. Nach Neustart beginnen wartende Abläufe nicht erneut; nur explizite Start- und Zeit-Trigger werden neu gestartet. Ein Start-Trigger wird erst bei bestehender WLAN-Verbindung ausgeführt.

Seit Version 2.0.2 werden Tor-Zustandswechsel direkt beim Dekodieren erfasst und gepuffert. Ein erster statischer Zustand erzeugt keine vermeintliche Bewegung; ein erster tatsächlich gemeldeter Bewegungszustand kann den passenden Trigger auslösen. Lichtänderungen werden weiterhin ungefähr alle 250 ms beobachtet. Einzelheiten und Grenzen stehen im Abschnitt zu Version 2.0.2.

Maximal 24 Blöcke, 48 Verbindungen, 16 wartende Schritte, eine Stunde Wartezeit und mindestens eine Sekunde Timerintervall. Kreise und doppelte Verbindungen werden beim Speichern abgewiesen. Wiederholungen sind mit Zeitintervall-Triggern möglich. JSON-Schreibdaten sind auf 1024 Zeichen, HTTP-Parameter auf 512 Zeichen begrenzt. WLED-Statusantworten sind auf 8192 Byte begrenzt (auch bei Chunked-Übertragung); weitergereichte Statusdaten sind auf 4096 Byte begrenzt. Bei großen Segmentkonfigurationen kann die Statusabfrage diese Grenzen überschreiten.

HTTP läuft in einer separaten Aufgabe mit niedriger Priorität; Buskommunikation und MQTT werden dadurch nicht auf eine HTTP-Antwort warten gelassen. Verbindungs- und Lese-Timeout betragen je 750 ms. Fehler beenden den betroffenen Zweig, ohne automatische Wiederholung eines möglicherweise bereits ausgeführten Befehls. Der letzte HTTP-Status und die Zahl wartender Schritte erscheinen unter der Blockpalette. Während einer Anfrage kann Speichern kurz „Beschäftigt“ melden; dann erneut versuchen.

Mehrere Trigger laufen unabhängig. Bereits wartende Schritte werden nicht durch einen neuen Torstatus abgebrochen. Speichern ersetzt den Ablauf vollständig und verwirft wartende Schritte. Export/Import sichert den gesamten Ablauf als JSON; ein Import wird erst nach Speichern ausgeführt.

## Quellen und Prüfung

- Ausgangsprojekt: https://github.com/Gifford47/HCPBridgeMqtt
- HTTP-API: https://kno.wled.ge/interfaces/http-api/
- JSON-API: https://kno.wled.ge/interfaces/json-api/

Browserprüfungen verwenden simulierte Bridge-Antworten. Ein Test an einem realen Hörmann-Antrieb und WLED-ESP wurde nicht durchgeführt. Vor dem Einsatz Presets, WLAN-Erreichbarkeit und die gewünschten Torstatuswechsel an der eigenen Hardware prüfen.
## Neu in Version 2.0.1: typisierte Variablen und WLED-XML

Unter **Drei-Balken-Menü → Variablen** bis zu 24 Variablen anlegen. Jede Variable hat einen Namen, einen Typ, einen Anfangswert und die Option „Dauerhaft“. Namen beginnen mit einem Buchstaben oder Unterstrich und dürfen anschließend auch Ziffern enthalten (maximal 24 Zeichen).

| Typ | Zulässiger Wert |
| --- | --- |
| `string` | Text mit maximal 256 UTF-8-Byte |
| `int` | Ganze Zahl von -2147483648 bis 2147483647 |
| `bool` | `true` / `false` oder `1` / `0` |

Variablen gelten global für den gespeicherten Ablauf. „Variable speichern“ übernimmt einen festen Wert oder ein Feld wie `bridge.currentPosition`, `wled.ps` oder `vars.andereVariable`. „Variable lesen“ gibt den aktuellen Wert mit seinem Typ weiter. Bedingungen können globale Variablen direkt über `vars.name` lesen, ohne vorher einen Statusblock zu verwenden.

Als „Dauerhaft“ markierte Werte werden bei Änderungen gesammelt ungefähr alle fünf Sekunden in den Flash geschrieben. Bei einem plötzlichen Stromausfall können Änderungen seit der letzten Sicherung fehlen. Unveränderte Werte verursachen keine weiteren Schreibvorgänge. Nicht dauerhafte Variablen starten nach Neustart oder Übernahme eines neuen Ablaufs mit ihrem Anfangswert. Dauerhafte Variablen behalten beim Speichern ihren aktuellen Wert, solange Name und Typ unverändert sind; der Anfangswert überschreibt diesen Wert nicht. Zum gezielten Ändern dient „Variable speichern“. Werksreset löscht Definitionen und gespeicherte Werte.

### Preset aus einer int-Variable

Der Preset-Block hat zusätzlich einen braunen **int-Eingang** unten links. Verbinde den Ausgang von „Variable lesen“, „Variable speichern“ oder „WLED /win → Variablen“ mit diesem Anschluss. Der Ausgang muss einer `int`-Variable entsprechen. Die Preset-Quelle wechselt beim Verbinden automatisch auf „Verbundener int-Eingang“.

Diese Verbindung übergibt den Wert **und startet den Preset-Block**. Es handelt sich nicht um eine dauerhaft anliegende Leitung, die auf einen zweiten Trigger wartet. Für eine Wiederherstellung genügt deshalb:

`Tor geschlossen → Variable lesen (vorherigesPreset) → int-Eingang von WLED Preset`

Nur Werte 1 bis 250 werden als Preset aufgerufen. Ein fehlender Eingangswert oder ein Wert außerhalb des Bereichs beendet diesen Zweig mit einer Fehlermeldung. Für eine feste Nummer weiterhin „Feste Preset-Nummer“ und den normalen oberen Eingang verwenden.

### Alle von /win gelieferten Werte zuordnen

„WLED /win → Variablen“ ruft exakt `http://<gespeicherte WLED-IP>/win` auf. Im Block lassen sich mehrere XML-Felder mit Index und Zielvariable eintragen. „Aktuelle WLED-Felder anzeigen“ zeigt nach dem Speichern die tatsächlichen Felder und ihre Werte vom Gerät, auch zusätzliche Felder neuerer WLED-Versionen. Diese Vorschau liest nur den Zustand und verändert keine Variablen.

| XML-Feld | Index | Beispiel-Zielvariable und Typ |
| --- | --- | --- |
| `ps` | 0 | vorherigesPreset (`int`) |
| `ac` | 0 | helligkeit (`int`) |
| `cl` | 0 / 1 / 2 | rot / gruen / blau (jeweils `int`) |
| `cs` | 0 / 1 / 2 | sekundärRot / sekundärGruen / sekundärBlau (`int`, Variablennamen ohne Umlaute verwenden) |
| `nl` | 0 | nachtlicht (`bool`) |
| `ds` | 0 | geraetename (`string`) |
| `fx`, `sx`, `ix`, `fp` | jeweils 0 | Effekt, Geschwindigkeit, Intensität und Palette (`int`) |

Index 0 bezeichnet den ersten Eintrag eines Feldes. Bei wiederholten Farbfeldern sind Index 0, 1 und 2 die Kanäle Rot, Grün und Blau. Bei verschachtelten Erweiterungen werden Blattfelder mit Punkten getrennt, beispielsweise `segment.id`. Es werden alle unterstützten Blattfelder dynamisch erfasst, ohne feste Liste im Firmwarecode. XML-Zeichenreferenzen wie `&amp;` und numerische Zeichenreferenzen werden in normalen Text zurückgewandelt.

Ein Block unterstützt bis zu 24 Zuordnungen. Jede Zielvariable darf darin nur einmal vorkommen. „Wert am Ausgang“ bestimmt, welche der zugeordneten Variablen der Block an Folgeblöcke weitergibt. Wähle hier eine `int`-Variable, wenn du den Preset-Eingang anschließen möchtest.

Alle Zuordnungen werden zuerst geprüft und dann gemeinsam übernommen. Falls ein Feld fehlt oder ein Wert nicht zum Zieltyp passt, werden keine Zielvariablen dieses Blocks geändert. Bool-Konvertierung akzeptiert ausschließlich true/false/1/0; beispielsweise Nachtlicht-Modus `nf=2` benötigt eine `int`-Variable. Die XML-Antwort ist auf 8192 Byte, 64 unterschiedliche Blattpfade und insgesamt 128 Blattwerte begrenzt. DTDs und externe Entitäten werden nicht verarbeitet.

**Grenze der HTTP-API:** `/win` liefert nur die Werte, die WLED in seiner XML-Statusantwort veröffentlicht. Es enthält nicht die vollständige Konfiguration sämtlicher Segmente und Funktionen. Für weitere Informationen steht der vorhandene JSON-Leseblock zur Verfügung; dessen Felder können anschließend mit „Variable speichern“ übernommen werden.

### Mitgeliefertes Beispiel

`examples/preset-sichern-wiederherstellen.json` ist beim Import zunächst deaktiviert:

- Tor öffnet → `/win` liest aktuelles Preset, Helligkeit, RGB, Nachtlicht und Gerätename in Variablen → festes Preset 2 aufrufen.
- Tor geschlossen → gespeichertes Preset auf Bereich 1–250 prüfen → Variable lesen → Preset über int-Eingang wiederherstellen.

Vor Aktivierung WLED-IP und das feste Preset 2 anpassen. Das Beispiel stellt die vorherige **Preset-Nummer** wieder her, nicht einen vollständig manuell veränderten LED-Zustand. Meldet WLED kein gültiges Preset, überspringt es die Wiederherstellung.

Die bisherigen Ablaufdateien ohne Variablen sind weiterhin nutzbar. Export enthält Definitionen und Anfangswerte, aber keine aktuellen Laufzeitwerte aus dem Flash. Firmware-Version, Systemanzeige und automatisch erzeugte Firmware-Dateinamen verwenden jetzt `2.0.1`. Wie gewünscht wurde diese Version nicht kompiliert oder geflasht.
## Version 2.0.2: WLED-Lesefix, Overlays, Sprachen und Diagnose

### Nachgewiesener WLED-Fehler

Auf der laufenden Bridge lieferte `/api/flows/status` dieselbe Antwort wie `/api/flows`: die Konfiguration statt Laufzeitwerten. Die bisher implizite URL-Erkennung des Webservers ordnete Unterpfade dem übergeordneten Konfigurationspfad zu. Alle eigenen Routen verwenden jetzt ausdrücklich `AsyncURIMatcher::exact`. Auch POST-Routen für manuellen Trigger, WLED-Abfrage und Sprache werden getrennt behandelt.

Die XML-Feldliste hat zusätzlich einen eigenen Abfragestatus unter GET/POST `/api/flows/win`, Anfrage-IDs, Wiederholungen bei kurzzeitigem HTTP 503 und sichtbare Fehler. Sie hängt nicht mehr vom allgemeinen Drei-Sekunden-Statuspoll ab. Die reine XML-Verarbeitung wurde am PC mit der echten Antwort von WLED `192.168.179.60` geprüft. Es wurden dabei keine Presets verändert.

### Variablen und Einstellungen

Variable lesen, Variable speichern und WLED-/win-Blöcke zeigen den aktuellen Wert direkt unter ihrem Namen an. Die Werte werden über die korrigierte Laufzeitstatus-Schnittstelle aktualisiert. Bis zum ersten gültigen Status steht dort ein Strich.

WLAN/MQTT/Gerät, Sensoren, Ein-/Ausgänge, Experteneinstellungen, WLED-Verbindung und System/Firmware öffnen in einem eigenen modalen Overlay. Sie erscheinen nicht mehr unter der Torübersicht. Die vorhandenen Speichern-/Neustart-Funktionen bleiben in den jeweiligen Formularen erhalten.

Unter Sprache stehen Deutsch und Englisch zur Verfügung. Die Dateien liegen separat in `WebUI/webpage/i18n/de.json` und `en.json`. Die Auswahl wird über `/api/ui/language` im nichtflüchtigen Speicher der Bridge abgelegt und bei jedem Seitenaufruf geladen, auch nach einem Neustart oder in einem anderen Browser. Ein Werksreset stellt Deutsch wieder her. Die Sprachdateien werden beim PlatformIO-Build zusammen mit den Seiten eingebettet. Anpassungen an Texten benötigen deshalb einen erneuten Build; ein Dateisystem-Upload ist nicht erforderlich.

### Erkennung von Öffnung und Positionswerten

Der aktuelle Positionswert kommt unabhängig von der Bewegungsrichtung aus dem unteren Byte von Register `0x9D32` (0x9D31 + 1). Das obere Byte enthält die Zielposition. Der Bereich 0–200 entspricht 0–100 Prozent. Eine Bedingung, die nur beim Öffnen die Positionsübernahme oder MQTT-Ausgabe verhindert, wurde im untersuchten Quellcode nicht gefunden. Deshalb ist ein tatsächlicher Busmitschnitt während der fehlerhaften Öffnung erforderlich, bevor eine bestimmte Ursache behauptet werden kann.

Die Verarbeitung wurde robuster gemacht:

- Jeder empfangene gültige Positionswert wird mit dem tatsächlich gespeicherten Positionszustand verglichen, nicht nur mit dem vorherigen Bibliotheksregister.
- Rohwerte über 200 werden als ungültig protokolliert, ohne einen Prozentwert über 100 zu erzeugen. Der letzte gültige Positionswert bleibt dabei erhalten.
- Eine monotone Änderungsnummer verhindert, dass die MQTT-Ausgabe eine Änderung allein durch das gemeinsame Zurücksetzen eines booleschen Flags verliert. Bei fehlgeschlagenem Einreihen wird erneut versucht.
- MQTT wird nun alle 200 ms statt alle 2000 ms bearbeitet. Die Webübersicht fragt jede Sekunde statt alle drei Sekunden ab.
- Tor-Zustandswechsel werden direkt beim Dekodieren in eine separate, nicht blockierende Warteschlange übernommen. Dadurch gehen sie während einer WLED-HTTP-Abfrage nicht allein durch das bisherige Polling verloren. Wiederholte gleiche Zustände werden nicht als neue Bewegung behandelt.
- Die Zustandswarteschlange umfasst 32 Ereignisse; Ereignisse älter als zehn Sekunden werden verworfen. Die Zahl verlorener Ereignisse und das letzte Torereignis stehen im Laufzeitstatus. Ohne gültige Buskommunikation ist weiterhin keine verlässliche Torerkennung möglich.

Die PC-Tests prüfen alle 201 gültigen Rohwerte beim Öffnen und Schließen sowie den echten Zustandsbeobachter. Sie ersetzen keine Messung am Hörmann-Antrieb.

### Busprotokoll

Im Drei-Balken-Menü **Busprotokoll** öffnen. Die Ansicht zeigt Zeit seit Start, Ereignistyp, Register, vorherigen und neuen Rohwert, Positions-/Zielwert, Zustand und Zusatzinformationen. Sie liest verarbeitete Modbus-Ereignisse aus einem RAM-Ringpuffer mit 512 Einträgen. Änderungen an zusätzlichen Statusregistern 0x9D31–0x9D39 und unbekannte Zustandskennungen werden ebenfalls erfasst. Regelmäßige Busanforderungen werden zur Übersicht einmal pro Sekunde zusammengefasst.

Die API lautet `GET /api/bus/log?after=<sequence>`. Antworten sind in Seiten mit maximal 32 Einträgen aufgeteilt, damit die ESP32-Speichernutzung begrenzt bleibt. Die Webansicht holt fehlende Seiten automatisch nach und hält bis zu 1000 Einträge im Browser. Die Tabellenansicht zeigt daraus die letzten 250. JSON herunterladen exportiert sämtliche aktuell im Browser gehaltenen Einträge.

Wichtige Ereignisse:

| Ereignis | Bedeutung |
| --- | --- |
| request | Modbus-Anforderung; function und extra enthalten Funktionscode bzw. Registeranzahl |
| position | Register 0x9D32 mit aktuellem und Ziel-Rohwert sowie übernommenen Prozentwerten |
| state | Zustandsregister 0x9D33 nach der Dekodierung |
| unknown_state | Eine bisher nicht unterstützte Zustandskennung |
| register | Änderung eines weiteren Statusregisters |
| lamp | Lichtregister |
| mqtt_queued | Zustand und Positionswert wurden erfolgreich in die MQTT-Sendewarteschlange eingereiht |
| mqtt_failed | Mindestens eine MQTT-Publish-Anfrage konnte nicht eingereiht werden |

`mqtt_queued` bestätigt das Einreihen, nicht den Empfang durch Home Assistant. Bei diesen MQTT-Zeilen stehen in previousRaw/raw die Paket-IDs der beiden Publish-Aufrufe und kein Busregisterinhalt.

Bei position-Ereignissen zeigt flags die Kombination folgender Bits: 1 = aktueller Rohwert gültig, 2 = Ziel-Rohwert gültig, 4 = aktuelle Position übernommen, 8 = Zielposition übernommen. currentRaw und targetRaw im JSON enthalten beide ursprünglichen Bytes. Der Rohwert 0xC864 bedeutet beispielsweise Ziel 200 (=100 %) und aktuell 100 (=50 %).

Für die Diagnose der Öffnung: Protokoll vor der Bewegung öffnen, das Tor am vorhandenen Bedienelement öffnen, danach JSON herunterladen. Für das Schließen einen weiteren Export anlegen. Dann lässt sich vergleichen, ob das Positionsregister auch beim Öffnen ankommt, welches Byte sich ändert, ob die Werte gültig sind und welche Zustandskennung erkannt wurde. Ungültige UART-/CRC-Telegramme erreichen diese Modbus-Registercallbacks nicht; das Protokoll enthält die von der Modbus-Bibliothek verarbeiteten Ereignisse.

### Serielle Diagnose im Browser

Unter **WLAN, MQTT & Gerät → Enable serial debug** bzw. **Serielle Diagnose aktivieren** die Ausgabe aktivieren, speichern und neu starten. Anschließend im Menü **Serielle Diagnose** öffnen.

Die Ansicht übernimmt die über DBG_PRINT/DBG_PRINTLN/DBG_PRINTF ausgegebenen Meldungen sowie die tatsächlich ausgegebenen ESP_LOG-Meldungen. Diese werden weiterhin über USB/Serial ausgegeben und zusätzlich in einem RAM-Puffer mit 96 abgeschlossenen Zeilen gehalten. Pro Zeile werden bis zu 160 Zeichen gespeichert; längere Zeilen sind im JSON als truncated markiert. Die API lautet `GET /api/debug/serial?after=<sequence>`. Neue Meldungen erscheinen etwa jede Sekunde. Pause hält die Webaktualisierung an; Download sichert die im Browser geladenen Zeilen als JSON. Das Einschalten der Ausgabe richtet keine Fernsteuerung für beliebige serielle Befehle ein.

Bus- und Serial-Protokoll verwenden RAM und werden bei Neustart oder Stromausfall gelöscht. Volle Puffer überschreiben die ältesten Einträge. Laufzeitaufzeichnung verursacht keine fortlaufenden Flash-Schreibvorgänge. Beide APIs und Sprachdateien verwenden den vorhandenen Web-Passwortschutz.

### Dein korrigierter Ablauf

`examples/dein-ablauf-korrigiert.json` enthält die von der Bridge gelesene Konfiguration mit der korrigierten Rückspeicherungskette:

`Tor geschlossen → Variable lesen PS → int-Eingang von WLED Preset`

Die Datei ist zum Prüfen nach dem Import zunächst deaktiviert. Das laufende Gerät wurde nicht verändert. Vor dem Speichern Einstellungen und Aktivierung kontrollieren.

### Build und Prüfung

Die Firmware bleibt Version 2.0.2. Es wurde wie gewünscht keine ESP32-Firmware kompiliert oder geflasht. Die eingebetteten Webdateien und Sprachdateien wurden ohne Compiler erzeugt. Browserprüfungen und isolierte PC-Tests sind in VALIDIERUNG.md beschrieben.

Zusätzlich korrigiert: Die bisherige Hilfsfunktion für `/statush` lehnte mit `responseAge() > 0` gerade frisch empfangene Pakete im Alter von null Sekunden ab. Sie akzeptiert nun bei gültigem Bus Alter 0 bis unter 3 Sekunden. Diese Inkonsistenz betraf den Hilfsstatus-Endpunkt; sie erklärt allein nicht ein gleichzeitiges Fehlen von Öffnungsdaten in allen Ausgabepfaden.
