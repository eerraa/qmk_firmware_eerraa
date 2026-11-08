======================================================================
Firmware Guide
======================================================================

■ Firmware-Update

Verwenden Sie die zur Tastatur passende Firmware-ZIP-Datei. Anleitungen zu Update und Sicherung sowie funktionsbezogene Hilfe (Help) finden Sie unter https://usekb.cc.

1. Versetzen Sie die Tastatur in den Bootloader-Modus.
   - Einstellungsansicht: SYSTEM -> BOOT -> Jump To BOOT
   - Taste QK_BOOT in der Tastenbelegung
   - USB anschließen, während die physische Taste ganz oben links gedrückt gehalten wird (Bootmagic)
   - Reset-Taste auf der Platine zweimal schnell drücken (oder RST-GND zweimal kurz überbrücken)
2. Kopieren Sie die reguläre Firmware .uf2 auf das Laufwerk RPI-RP2 (automatischer Neustart nach Abschluss).
3. Prüfen Sie nach dem Update die Version unter SYSTEM -> VERSION.

Installieren Sie bei Split-Tastaturen dieselbe Datei .uf2 auf beiden Hälften. Wird die Übertragung unterbrochen oder funktioniert die Tastatur nicht ordnungsgemäß, wechseln Sie erneut in den Bootloader und kopieren Sie dieselbe Datei nochmals. Ein reguläres Update behält gültige Einstellungen desselben Speicherformats bei; eine Formatänderung oder ungültige Speicherdaten können jedoch ein Zurücksetzen auslösen, weshalb Sie vorab eine Sicherung erstellen sollten. Da ältere Firmware beim Einstieg über Bootmagic zurückgesetzt werden kann, nutzen Sie beim ersten Update VIA BOOT oder zweimaliges Drücken von Reset.

■ Hinweis zu Reset.uf2 (Einstellungen zurücksetzen)

Installieren Sie Reset.uf2 aus derselben ZIP-Datei nur dann, wenn die Einstellungen zurückgesetzt werden müssen. Nur beim ersten Start nach der Installation werden Tastenbelegung, Makros und Benutzereinstellungen vollständig zurückgesetzt; danach kann die Tastatur direkt weiterverwendet werden, ohne die reguläre Firmware erneut aufzuspielen. Bei späteren Neustarts bleiben neue Einstellungen erhalten, und eine erneute Installation derselben Reset.uf2 setzt sie noch einmal zurück. Eine abgebrochene Übertragung gilt nicht als abgeschlossene Installation. Wird während des Zurücksetzens die Stromversorgung getrennt, wird der Vorgang erneut versucht; startet der normale Betrieb nicht, installieren Sie dieselbe Datei über den Hardware-Bootloader erneut. Ein sicheres Löschen oder die Behebung physischer Flash-Defekte wird nicht garantiert.

Trennen Sie beim Zurücksetzen einer Split-Tastatur die Verbindung zwischen beiden Hälften, führen Sie die Installation derselben Reset.uf2 sowie das Zurücksetzen auf jeder Hälfte separat vollständig durch und verbinden Sie beide Hälften erst danach wieder.

■ Tastatureinstellungen

Nutzen Sie für Tastatureinstellungen und erweiterte Funktionen https://usekb.cc sowie Help neben dem jeweiligen Menü. Hinweise zur Verwendung des offiziellen VIA (https://usevia.app) finden Sie in usevia.app/usevia.txt.
