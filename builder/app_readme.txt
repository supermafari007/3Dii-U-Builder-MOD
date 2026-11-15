3Dii U Builder {version} (beta)
=========================

3Dii U Builder convierte tus propios volcados de juegos de 3DS en juegos que puedes instalar en tu Wii U. No necesitas instalar nada más: esta carpeta contiene el programa completo.


REQUISITOS

- Un PC de 64 bits con Windows 10 u 11. Mientras se compila un juego, la unidad C: necesita espacio libre equivalente a unas tres veces el tamaño del juego.
- Un volcado de tu juego de 3DS: archivo .3ds, .cci, .cia, .cxi o .app, cifrado o descifrado. También puedes incluir su actualización y DLC.
- Para volcados cifrados: los archivos boot9.bin y seeddb.bin de tu propia 3DS, extraídos con GodMode9. Colócalos en C:\Users\<you>\.3ds o selecciona su carpeta en Builder.
  - boot9.bin: abre [M:] MEMORY VIRTUAL, selecciona boot9.bin y pulsa Y; después abre [0:] SDCARD, vuelve a pulsar Y y elige Copy.
  - seeddb.bin: pulsa HOME, selecciona More... y luego Build support files. El archivo se guardará en gm9/out, en la tarjeta SD.
  Cada fila de «Tus archivos» en Builder tiene un botón «Info» con estos pasos. También están disponibles en las preguntas y respuestas de la página Ayuda.
- El archivo otp.bin de tu Wii U, extraído de una copia de seguridad de la NAND de tu propia consola. El juego se empaqueta con él. Builder nunca muestra ni almacena la clave.
- Una Wii U con Aroma, WUP Installer GX2 y su tarjeta SD.
- Los archivos del sistema de tu 3DS: algunos archivos pequeños que cada 3DS conserva en su propia memoria, como la fuente del sistema que muchos juegos utilizan para mostrar texto, los componentes con los que se dibujan los personajes Mii y la lista de palabras prohibidas que los juegos consultan al escribir nombres. Los juegos los cargan desde la consola, por lo que no están en los volcados: los juegos que utilizan Miis se detienen si faltan sus datos, y Tomodachi Life lo hace si falta la lista de palabras. Puedes obtenerlos desde tu propia 3DS de cualquiera de estas maneras:
  - Azahar: Archivo > Set Up System Archivos, con Artic Setup Tool en ejecución en tu 3DS. Builder los encontrará automáticamente.
  - GodMode9: en [1:] SYSNAND CTRNAND > title están las dos carpetas: 0004009b (fuente y datos Mii) y 000400db (lista de palabras). Selecciona 0004009b y pulsa Y; abre [0:] SDCARD, vuelve a pulsar Y y elige Copy (no Move). Después copia 000400db de la misma manera, junto a la anterior. En Builder, pulsa «Carpeta» junto a «Archivos del sistema de 3DS» y selecciona la carpeta 0004009b. Al copiarlos de esta manera, los archivos quedan cifrados, por lo que Builder necesita las claves de tu 3DS (boot9.bin) para leerlos.
  Builder también busca estos archivos en la carpeta de usuario de Citra y en Descargas, Escritorio y Documentos. Los copia una sola vez y los conserva para los juegos siguientes.


PRIMEROS PASOS

1. Descomprime toda la carpeta «3Dii U Builder» en una ruta corta, por ejemplo C:\Games. Mantén la carpeta engine junto al archivo exe y no ejecutes el programa desde dentro del ZIP.
   Si Windows indica que una ruta es demasiado larga al descomprimir, o Builder informa que faltan archivos, vuelve a extraerlo en una carpeta con una ruta más corta.
2. Abre «3Dii U Builder.exe». Esta versión beta no está firmada, por lo que Windows podría indicar que «protegió tu PC»: pulsa «Más información» y luego «Ejecutar de todas formas».
3. Pulsa el recuadro grande con el símbolo «+» (o Ctrl+O) y selecciona tu volcado. Builder te preguntará por la actualización, el DLC y los mods del juego.
4. En «Tus archivos», cada indicador se volverá cian cuando Builder disponga del archivo correspondiente.
5. En «Destino», selecciona tu tarjeta SD o una carpeta y pulsa «¡Compilar!».
   Los juegos grandes tardan más: el código del juego se recompila para Wii U y sus shaders se compilan por adelantado.
   La primera compilación también compila el propio entorno de ejecución de Builder, por lo que tarda unos minutos más que las siguientes.
6. Inserta la tarjeta SD en la Wii U e instala el juego con WUP Installer GX2.
   Si ya habías instalado una versión anterior del mismo juego, elimínala primero (Configuración de la consola > Gestión de datos) y luego instala la nueva. Al eliminar un juego también se eliminan sus partidas guardadas.


COMPATIBILIDAD DE JUEGOS

- «Completo en Wii U»: juego terminado. Se confirmó en una Wii U real que funciona como debería, sin problemas conocidos pendientes:
  New Super Mario Bros. 2.
- «Jugable en Wii U»: probado en una Wii U real y jugable, aunque algunos aspectos todavía pueden presentar problemas:
  Tomodachi Life, Paper Mario: Sticker Star, Mario & Luigi: Dream Team, Mario & Luigi: Paper Jam,
  Mario & Luigi: Superstar Saga + Bowser's Minions, Mario & Luigi: Bowser's Inside Story + Bowser Jr.'s Journey,
  Professor Layton and the Azran Legacy y Professor Layton and the Miracle Mask.
  Cualquiera de estas etiquetas se muestra cuando tu volcado corresponde a la misma edición que se probó. Estos juegos se probaron sin actualizaciones.
- «Funciona en Cemu» (página Games): compilados con Builder y ejecutados en el emulador Cemu, pero todavía no probados en una Wii U real:
  Mario Kart 7, The Legend of Zelda: A Link Between Worlds, Metroid: Samus Returns, Kirby: Triple Deluxe, Rhythm Heaven Megamix,
  Phoenix Wright: Ace Attorney Trilogy, Animal Crossing: New Leaf, Kid Icarus: Uprising, Super Mario 3D Land y Miitopia.
- «Sin probar»: cualquier otro juego y todos los juegos con mods. Puede funcionar, no iniciar o detenerse a mitad de la ejecución.
  Los informes sobre estos juegos son especialmente útiles.


SI UN JUEGO FUNCIONA LENTAMENTE O SE CONGELA EN WII U

- Desactiva los plugins SDCafiine y FTP (FTPiiU) mientras juegas: abre el menú de plugins de Aroma (L + cruceta Abajo + Menos), desactiva ambos y vuelve a iniciar el juego. Aroma incluye FTPiiU activado de forma predeterminada. Utiliza los mismos núcleos del procesador que el juego y puede ralentizarlo: algunos juegos llegaron a avanzar muy lentamente o congelarse con el sonido en bucle hasta que se desactivó. Con SDCafiine activado, un evaluador observó que New Super Mario Bros. 2 funcionaba muy lentamente en todas las zonas.
- La primera vez que un juego muestra un tipo nuevo de escena, puede producirse una breve interrupción mientras la consola genera los shaders de esa escena. La consola los conserva, así que las siguientes veces se muestran con fluidez.


ACTUALIZACIONES

- Al iniciarse, Builder consulta en GitHub la lista de versiones disponibles; solo descarga la que tú elijas. Si existe una versión más reciente, la página Build la ofrece. También puedes volver a comprobarlo en cualquier momento desde Ayuda > Buscar actualizaciones.
- Pulsa «Actualizar» y la nueva versión se descargará en la carpeta de datos de Builder: %LOCALAPPDATA%\3Dii U Builder\app. A partir de entonces se abrirá esa versión: inmediatamente si eliges «Reiniciar ahora» o la próxima vez que inicies Builder. La carpeta que descomprimiste originalmente nunca se modifica.
- Si una actualización falla: mantén pulsada la tecla Shift mientras inicias Builder para abrir la copia que descomprimiste, o elimina la carpeta %LOCALAPPDATA%\3Dii U Builder\app para volver a ejecutar la copia original. Una versión que no logra iniciarse se aparta automáticamente, y Ayuda > Buscar actualizaciones vuelve a ofrecerla.


INFORMAR DE UN PROBLEMA

- Si falla una compilación, pulsa «Detalles» y copia el contenido que aparece.
- Indica qué juego es (título y región) y si agregaste una actualización, DLC o un mod.
- Si el problema ocurre en Wii U, explica en qué parte del juego sucede y qué observaste.


QUÉ SE CONSERVA EN TU PC

- Builder trabaja en %LOCALAPPDATA%\3Dii U Builder.
  Allí conserva el entorno de ejecución compilado para el siguiente juego y hasta 2 GB del código compilado de cada juego. Así, si vuelves a compilar un juego (con su actualización o con una versión nueva de Builder), solo se recompila lo que haya cambiado. Los demás archivos de trabajo de cada juego se eliminan al finalizar la compilación.
  Eliminar esa carpeta libera el espacio; Builder la volverá a crear cuando sea necesario.
- Los juegos no utilizan la tarjeta SD para guardar datos: sus partidas, ajustes y los shaders que genera la consola permanecen en la propia Wii U.
- Tus volcados nunca se modifican. Tus claves solo se leen.
- Los juegos que compilas se crean a partir de tus volcados y tus claves: no los compartas.

- Al seleccionar un juego, Builder descarga su portada desde GameTDB (art.gametdb.com) para las pantallas de inicio y el icono del menú de Wii U.
  Solo envía las cuatro letras del código de producto del juego. La etiqueta «Art» del recuadro permite elegir una imagen propia para las pantallas de inicio, otra para el icono del menú de Wii U o quitarlas para utilizar el icono del propio juego.
  La opción «GamePad bezel» permite elegir una imagen que el GamePad muestra junto a las pantallas, como marco u overlay, en lugar del fondo negro. Puede ser cualquier imagen: se ajusta para cubrir el GamePad (854x480) y se recortan los bordes cuando su proporción no coincide. «GamePad start screen» asigna una imagen de inicio exclusiva para el GamePad, que se muestra tal cual (la pantalla del televisor conserva la suya), y «This game's sound» asigna un sonido de inicio exclusivo para ese juego, en lugar del sonido general.
- Con una clave de SteamGridDB (página Ayuda, sección «Imagen y sonido»; la clave es gratuita y se obtiene en steamgriddb.com, en Preferences > API), cada juego obtiene de SteamGridDB un banner para las pantallas de inicio y un icono para el menú de Wii U. Builder envía el nombre del juego (y sus nombres en otras regiones si SteamGridDB no reconoce esta edición) y tu clave; no envía nada más. La clave permanece en los ajustes de Builder en este PC.
- Puedes agregar tu propio sonido de inicio desde Ayuda > «Imagen y sonido»: selecciona un archivo WAV y todos los juegos que compiles lo reproducirán al iniciar.
- Builder consulta en GitHub la lista de versiones disponibles y solo descarga una cuando pulsas «Actualizar».

Builder no incluye juegos, datos de juegos ni claves. Esta es una versión beta y se proporciona sin garantía.
El archivo Third-party notices.txt enumera el software de código abierto que incluye.
