======================================================================
Split Firmware Guide
======================================================================

■ Actualización de firmware

Utilice el archivo ZIP de firmware correspondiente a su teclado e instale siempre el mismo archivo .uf2 en las unidades izquierda y derecha del teclado dividido. No se admite la combinación de versiones de firmware distintas.

Realice el siguiente procedimiento en una unidad cada vez.

1. Cambie la unidad al modo Bootloader.
   - Pantalla de configuración del teclado: SYSTEM -> BOOT -> Jump To BOOT
   - Tecla QK_BOOT asignada en el mapa de teclas
   - Conectar el USB manteniendo pulsada la tecla física superior izquierda de la unidad que va a flashear (Bootmagic)
   - Pulsar rápidamente dos veces el botón Reset de esa unidad o puentear brevemente RST y GND dos veces seguidas (separándolos por completo entre cada contacto)
2. Cuando aparezca la unidad RPI-RP2 en el PC, copie el archivo .uf2 de firmware estándar (se reiniciará automáticamente al finalizar).
3. Instale el mismo archivo .uf2 en la unidad opuesta y compruebe la versión en SYSTEM -> VERSION de cada unidad.

En el firmware anterior, entrar mediante Bootmagic puede restablecer los ajustes; para la primera actualización de cada unidad, utilice VIA BOOT o pulse Reset dos veces. Las actualizaciones estándar conservan los mapas de teclas, macros y ajustes válidos del mismo formato de almacenamiento, pero un cambio en el formato de almacenamiento o datos guardados no válidos pueden restablecer los valores predeterminados independientemente del método de entrada. Antes de actualizar o ejecutar EEPROM CLEAN, haga una copia de seguridad del mapa de teclas mediante VIA SAVE + LOAD. No utilice Reset.uf2 para una actualización estándar.

■ Restablecimiento de recuperación de ajustes

El archivo Reset.uf2 del mismo ZIP incluye todas las funciones del firmware estándar. Restablece todos los ajustes actuales (mapa de teclas, macros y ajustes del usuario) únicamente en el primer inicio tras la instalación, y permite usar el teclado directamente sin volver a instalar el firmware estándar. Si es posible, haga antes una copia de seguridad.

Desconecte el cable dividido e inicialice cada unidad por separado. Si solo inicializa un lado, el lado opuesto no inicializado puede restaurar los ajustes anteriores.

1. Ponga cada unidad en modo Bootloader y copie el mismo archivo Reset.uf2.
2. Espere hasta que termine el restablecimiento de cada unidad y vuelva a conectarse como teclado.
3. Cuando ambos lados hayan terminado, vuelva a conectar el cable dividido y configure el teclado de nuevo para usarlo.

En los reinicios posteriores se conservan los ajustes recién guardados, y si vuelve a instalar el mismo Reset.uf2 se restablecerán una vez más. Una transferencia interrumpida no es una instalación completa, por lo que deberá volver a cargarlo. Si se corta la alimentación durante el restablecimiento, lo reintentará; si no comienza el funcionamiento normal, vuelva a cargar el mismo Reset.uf2 mediante el método de entrada al Bootloader por hardware de esa unidad. No borra todo el chip ni el bootloader, ni garantiza un borrado seguro o la reparación de fallos físicos de la memoria flash.

■ Configuración del teclado

Para la configuración del teclado y las funciones avanzadas, se recomienda utilizar https://usekb.cc y el botón Help junto a cada menú de configuración. Los modos de entrada avanzados y los ajustes de mantenimiento (hold) de Tap Dance son compatibles en usekb.cc, mientras que VIA oficial solo edita las acciones básicas y Term.

Para utilizar directamente VIA oficial (https://usevia.app), consulte usevia.app/usevia.txt en el ZIP de distribución sobre cómo cargar Draft Definition izquierdo/derecho, los ajustes exclusivos para teclados divididos y la indicación STATUS de enlace y sincronización.
