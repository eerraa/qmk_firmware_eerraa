======================================================================
Split Firmware Guide
======================================================================

■ Firmware-Update

Verwenden Sie die zur Tastatur passende Firmware-ZIP-Datei und installieren Sie auf der linken und rechten Hälfte der Split-Tastatur unbedingt dieselbe Datei .uf2. Die Kombination unterschiedlicher Firmware-Versionen wird nicht unterstützt.

Führen Sie die folgenden Schritte nacheinander für jede Hälfte durch.

1. Versetzen Sie die Hälfte in den Bootloader-Modus.
   - Tastatureinstellungen: SYSTEM -> BOOT -> Jump To BOOT
   - In der Tastenbelegung platzierte Taste QK_BOOT
   - USB anschließen, während die physische Taste ganz oben links der zu flashenden Hälfte gedrückt gehalten wird (Bootmagic)
   - Reset-Taste der jeweiligen Hälfte zweimal schnell drücken oder RST und GND zweimal schnell kurz verbinden (zwischen den Kontakten vollständig trennen)
2. Sobald das Laufwerk RPI-RP2 auf dem PC erscheint, kopieren Sie die reguläre Firmware .uf2 (automatischer Neustart nach Abschluss).
3. Installieren Sie dieselbe Datei .uf2 auch auf der anderen Hälfte und prüfen Sie auf jeder Hälfte unter SYSTEM -> VERSION die Version.

Da ältere Firmware beim Einstieg über Bootmagic die Einstellungen zurücksetzen kann, verwenden Sie beim ersten Update jeder Hälfte VIA BOOT oder zweimaliges Drücken von Reset. Ein reguläres Update behält gültige Tastenbelegungen, Makros und Einstellungen desselben Speicherformats bei, doch eine Änderung des Speicherformats oder ungültige Speicherdaten können unabhängig von der Einstiegsmethode zum Zurücksetzen auf die Standardwerte führen. Sichern Sie Ihre Tastenbelegung vor einem Update oder vor EEPROM CLEAN über VIA SAVE + LOAD. Verwenden Sie Reset.uf2 nicht für ein reguläres Update.

■ Zurücksetzen zur Wiederherstellung der Einstellungen

Die Datei Reset.uf2 in derselben ZIP-Datei enthält sämtliche Funktionen der regulären Firmware. Sie setzt nur beim ersten Start nach der Installation alle aktuellen Einstellungen (Tastenbelegung, Makros, Benutzereinstellungen) zurück und kann direkt als Tastatur weiterverwendet werden, ohne die reguläre Firmware erneut aufzuspielen. Erstellen Sie nach Möglichkeit vorab eine Sicherung.

Trennen Sie das Split-Kabel und setzen Sie jede Hälfte einzeln zurück. Wenn nur eine Hälfte zurückgesetzt wird, kann die nicht zurückgesetzte andere Hälfte die bisherigen Einstellungen wiederherstellen.

1. Versetzen Sie jede Hälfte in den Bootloader und kopieren Sie jeweils dieselbe Reset.uf2.
2. Warten Sie, bis das Zurücksetzen auf der jeweiligen Hälfte abgeschlossen ist und sie sich wieder als Tastatur verbindet.
3. Sobald beide Hälften fertig sind, schließen Sie das Split-Kabel wieder an und richten Sie die Tastatur neu ein.

Bei späteren Neustarts bleiben neu gespeicherte Einstellungen erhalten; wird dieselbe Reset.uf2 erneut installiert, erfolgt ein weiteres einmaliges Zurücksetzen. Eine abgebrochene Übertragung ist keine abgeschlossene Installation, sodass die Datei erneut übertragen werden muss. Bei einem Stromausfall während des Zurücksetzens wird der Vorgang erneut versucht; startet der normale Betrieb nicht, übertragen Sie dieselbe Reset.uf2 erneut über den Hardware-Bootloader-Einstieg der betreffenden Hälfte. Weder der gesamte Chip noch der Bootloader werden gelöscht, und ein sicheres Löschen oder die Reparatur physischer Flash-Defekte wird nicht garantiert.

■ Tastatureinstellungen

Für Tastatureinstellungen und erweiterte Funktionen werden https://usekb.cc und Help neben dem jeweiligen Einstellungsmenü empfohlen. Erweiterte Eingabemodi und Halte-Einstellungen für Tap Dance werden auf usekb.cc unterstützt, während das offizielle VIA nur Grundaktionen und Term bearbeitet.

Wenn Sie das offizielle VIA (https://usevia.app) direkt verwenden möchten, beachten Sie usevia.app/usevia.txt in der Release-ZIP zum Laden der linken/rechten Draft Definition, zu Split-spezifischen Einstellungen sowie zur STATUS-Anzeige für Verbindung und Synchronisierung.
