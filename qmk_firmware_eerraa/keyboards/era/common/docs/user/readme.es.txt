======================================================================
Firmware Guide
======================================================================

■ Actualización de firmware

Utilice el archivo ZIP de firmware correspondiente a su teclado. Puede consultar las instrucciones de actualización y copia de seguridad, así como la ayuda por función (Help), en https://usekb.cc.

1. Cambie el teclado al modo Bootloader.
   - Pantalla de ajustes: SYSTEM -> BOOT -> Jump To BOOT
   - Tecla QK_BOOT en el mapa de teclas
   - Conectar el USB manteniendo pulsada la tecla física superior izquierda (Bootmagic)
   - Pulsar dos veces seguidas el botón Reset de la placa (o puentear brevemente RST-GND dos veces)
2. Copie el archivo .uf2 de firmware estándar en la unidad RPI-RP2 (se reiniciará automáticamente al finalizar).
3. Tras la actualización, compruebe la versión en SYSTEM -> VERSION.

En los teclados divididos, instale el mismo archivo .uf2 en ambas unidades. Si la transferencia se interrumpe o el teclado no funciona con normalidad, vuelva a entrar en el Bootloader y copie de nuevo el mismo archivo. Las actualizaciones estándar conservan los ajustes válidos que usen el mismo formato de almacenamiento, pero un cambio de formato o datos guardados no válidos pueden provocar un restablecimiento, por lo que debe hacer una copia de seguridad antes. En el firmware anterior, entrar mediante Bootmagic puede restablecer los ajustes; para la primera actualización, utilice VIA BOOT o pulse Reset dos veces.

■ Precaución con Reset.uf2 (restablecimiento de ajustes)

Instale el archivo Reset.uf2 del mismo ZIP únicamente cuando necesite restablecer los ajustes. Restablece el mapa de teclas, las macros y todos los ajustes del usuario solo en el primer inicio tras la instalación, y el teclado puede seguir utilizándose directamente sin necesidad de volver a instalar el firmware estándar. En los reinicios posteriores se conservan los nuevos ajustes, y si vuelve a instalar el mismo Reset.uf2 se restablecerán una vez más. Una transferencia interrumpida no constituye una instalación completa. Si se corta la alimentación durante el restablecimiento, lo reintentará; si no comienza el funcionamiento normal, vuelva a instalar el mismo archivo mediante el Bootloader por hardware. No garantiza un borrado seguro ni la reparación de fallos físicos de la memoria flash.

Al restablecer un teclado dividido, desconecte la conexión entre ambos lados, complete la instalación del mismo Reset.uf2 y el restablecimiento en cada unidad por separado y, a continuación, vuelva a conectarlas.

■ Configuración del teclado

Para la configuración del teclado y las funciones avanzadas, utilice https://usekb.cc y el botón Help junto a cada menú. Para el uso de VIA oficial (https://usevia.app), consulte usevia.app/usevia.txt.
