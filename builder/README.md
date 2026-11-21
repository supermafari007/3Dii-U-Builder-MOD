# 3Dii-U Builder MOD

**Fork modificado del 3Dii U Builder original** ([J-Glitch2502/3dii-u](https://github.com/J-Glitch2502/3dii-u)), publicado aquí como **3Dii-U Builder MOD 1.0.0**. Conserva los créditos, avisos y licencias del proyecto original; el código fuente de esta versión está en este repositorio.

Convierte tus propios volcados de juegos de 3DS en juegos para Wii U. Elige un juego, indica si quieres incluir su actualización, DLC y algún mod, pulsa **¡Compilar!** y el juego quedará en tu tarjeta SD, listo para instalarse con WUP Installer GX2.

Builder no incluye datos de juegos ni claves. Todo lo que utiliza es tuyo: tus volcados, las claves de tu 3DS si están cifrados y el archivo `otp.bin` de tu Wii U.

## Cómo utilizarlo

1. Abre **`3Dii U Builder.exe`** en `dist/3Dii U Builder`. Esa carpeta contiene toda la aplicación: puedes copiarla donde quieras, ya que no necesita Python ni una cadena de herramientas instalada. Para ejecutar Builder desde el código fuente, haz doble clic en `3Dii U Builder.pyw`.
2. Pulsa **Elegir un juego** (el recuadro grande con el símbolo **+**, o Ctrl+O) y selecciona un volcado. Puede ser un archivo `.3ds`, `.cci`, `.cia`, `.cxi` o `.app`, cifrado o descifrado. El recuadro muestra la imagen del juego y, debajo, su nombre y desarrollador. Una etiqueta indica qué tan bien se sabe que funciona: **Completo en Wii U** para un juego terminado, **Jugable en Wii U** para los demás juegos probados y **Sin probar** para el resto.
3. Builder te preguntará por la **actualización** y el **DLC** directamente en su ventana:
   - Si están en la misma carpeta que el juego, mostrará sus nombres y preguntará si quieres utilizarlos.
   - Si no están allí, preguntará si los tienes y te permitirá seleccionar los archivos.
   - Más adelante puedes cambiar o quitar cualquiera de ellos desde **Opciones adicionales**.
   - Después preguntará por un **mod** (consulta [Mods](#mods)): selecciona su archivo `.zip` o su carpeta. También puedes gestionarlo desde **Opciones adicionales**.
4. **Tus archivos**: los indicadores se vuelven cian cuando Builder dispone de todo lo necesario.
   - **3DS keys** solo se necesitan para volcados cifrados. Builder los descifra siguiendo el procedimiento de `cia_to_decrypted_3ds.py`: makerom convierte la CIA del juego en un archivo `.3ds` y después pyctr lo descifra, utilizando `boot9.bin` y `seeddb.bin` de tu propia 3DS. Los busca en la carpeta `.3ds` de tu usuario (`C:\\Users\\<you>\\.3ds`), que es la ubicación propia de pyctr. Si los tienes en otro lugar, selecciona esa carpeta. GodMode9 puede extraer ambos archivos de una 3DS.
   - **Wii U otp.bin** proviene de una copia de seguridad de la NAND de tu propia consola. El WUP se empaqueta con la clave que contiene; Builder nunca muestra ni almacena esa clave.
5. **Destino**: elige tu tarjeta SD (las tarjetas preparadas para Wii U aparecen primero) o una carpeta.
6. **¡Compilar!**: el proceso tarda un tiempo, especialmente con juegos grandes. El código del juego se recompila para Wii U y sus shaders se compilan por adelantado. Verás cómo se completa cada etapa. Mientras tanto, puedes seguir usando el PC; **Cancelar** detiene el proceso de forma segura y **Detalles** muestra todo lo que imprime la compilación.
7. Inserta la tarjeta en tu Wii U e instala el juego con **WUP Installer GX2**. Si ya habías instalado una versión anterior del mismo juego, elimínala primero (Configuración de la consola, Gestión de datos) y luego instala la nueva.

La sección **Juegos**, en la barra izquierda, conserva los juegos que seleccionaste anteriormente y muestra los que se han probado en una Wii U real. **Ayuda** responde preguntas sobre los requisitos y contiene la versión, la comprobación de actualizaciones y la opción de apariencia clara u oscura.

Las partidas se guardan en el almacenamiento de guardado propio del juego en la consola, igual que en cualquier juego instalado. Puedes verlas en Gestión de datos.

## Compatibilidad de juegos

`compat.json` enumera los juegos probados en una Wii U real. Se compilan con los mismos identificadores de título que las versiones distribuidas anteriormente, por lo que volver a compilar uno reemplaza la versión antigua. Los siguientes juegos funcionan:

- Tomodachi Life
- New Super Mario Bros. 2
- Paper Mario: Sticker Star
- Mario & Luigi: Dream Team
- Mario & Luigi: Paper Jam
- Mario & Luigi: Superstar Saga + Bowser's Minions
- Mario & Luigi: Bowser's Inside Story + Bowser Jr.'s Journey
- Professor Layton and the Azran Legacy
- Professor Layton and the Miracle Mask

Cualquier otro juego se compila como **sin probar**. Recibe su propio identificador de título (`000500003Dxxxxxx`, reservado para evitar conflictos con títulos reales de Wii U) y se ejecutan las herramientas de búsqueda de código, pero todavía nadie lo ha jugado: puede que no inicie o que se detenga a mitad de la ejecución.

**Las actualizaciones y el DLC** se leen de la misma manera que en una 3DS:

- Una actualización aporta su propio código y su propio RomFS, que el juego superpone a los archivos originales.
- El DLC aporta un RomFS por cada contenido; el juego lo encuentra mediante las mismas llamadas que realiza en una 3DS.
- Los nueve juegos probados se probaron sin actualizaciones. Por tanto, si les agregas una, también pasan a considerarse sin probar.

## Mods

Los mods creados para Luma3DS en una 3DS funcionan directamente: puedes usar un archivo `.zip` o una carpeta que contenga `luma/titles/<title id>/` (o solamente el contenido de esa carpeta). Builder incorpora el mod al juego antes de recompilarlo, por lo que el código del mod se ejecuta como código recompilado, igual que el del propio juego.

- `romfs/`: archivos que reemplazan los del juego o agregan otros nuevos, como hace LayeredFS de Luma. Los nombres de archivo no distinguen mayúsculas de minúsculas, igual que en la tarjeta SD. Si un archivo también existe en la actualización del juego, se incorpora a los archivos de la actualización, que son los que el juego lee primero.
- `code.ips` o `code.bps`: parche para el código del juego. Si un parche BPS fue creado para otra versión del código (otra región, o con o sin actualización), se rechaza y se indica el motivo.
- `code.bin`: reemplaza el código completo. `exheader.bin`: encabezado extendido, necesario para un mod que cambie el tamaño del código.
- Si un mod modifica el código o los módulos CRO donde algunos juegos guardan la mayor parte de su código, se vuelve a ejecutar la búsqueda de código. En un juego probado, los resultados se suman a los encontrados en sus compilaciones de prueba.
- La carpeta del título debe corresponder al juego: si el mod es de otra región o edición, se rechaza y se indican los identificadores de título para los que fue creado. Cuando hay una actualización instalada, los mods de Luma utilizan el identificador de título propio del juego, y Builder hace lo mismo.
- Los plugins (`luma/plugins/<id>/*.3gx`, como los que utiliza CTGP-7) no son mods de este tipo. Modifican el juego en memoria mientras se ejecuta, y el código recompilado nunca vuelve a leer su código ARM; por eso no pueden incorporarse a la compilación.
- Al utilizar un mod, cualquier juego pasa a considerarse sin probar.

`python builder/pipeline.py --mod <zip or folder>` permite aplicar varios mods, en orden y cada uno sobre el resultado del anterior.

## Requisitos en el PC

Por ahora se necesita Windows 10 u 11 (Linux está previsto):

- **La aplicación independiente no necesita nada más.**
  - Incluye su propio Python, Qt, pyctr y makerom.
  - Su carpeta `engine` contiene el resto: el recompilador, el código fuente del entorno de ejecución, la cadena de herramientas PowerPC de devkitPro con wut, el compilador de shaders CafeGLSL y un generador de shaders ya compilado.
  - Compila en `%LOCALAPPDATA%\3Dii U Builder`, donde conserva el entorno de ejecución compilado para el siguiente juego.
  - Junto a él, `objcache` conserva hasta 2 GB de código de juegos compilado, clasificado según sus entradas (`tools/win/objcache.py`). Si vuelves a compilar un juego con una versión más reciente de la aplicación o con su actualización, solo se recompilan los archivos cuyo código haya cambiado.
- Las imágenes de los juegos se obtienen de Internet al seleccionar cada juego (`covers.py`). Puedes sustituirlas por imágenes propias —una para las pantallas de inicio y otra para el icono— desde el menú **Arte**, o quitarlas para utilizar el icono propio del juego. En modo **Consola Virtual**, Builder coloca la carátula dentro de la plantilla minimalista de Nintendo 3DS incluida en `phase0/title_code/meta`; produce las pantallas de TV a 1280×720 y GamePad a 854×480. La vista previa muestra TV, GamePad e icono de Wii U (128×128) antes de compilar. El nombre y distribuidor se leen del volcado y se pueden editar desde **Editar información**; se guardan por juego y aparecen en la plantilla. Se conserva también el marco del GamePad (`make_game_title.bezel_pixels`), las imágenes exclusivas y el sonido propio de cada juego.
  - Si introduces tu clave de SteamGridDB (página Help, sección «Art and sound»; se guarda en `settings.json`, nunca en el repositorio), se busca un banner panorámico para las pantallas de inicio y un icono cuadrado para el menú de Wii U. La búsqueda utiliza el nombre del juego o los nombres de otras regiones cuando SteamGridDB no reconoce la edición seleccionada (por ejemplo, allí Luigi's Mansion 2 aparece como Dark Moon).
  - Si no hay una clave o SteamGridDB no encuentra imágenes, se utiliza la portada de GameTDB, mediante las cuatro letras del código de producto.
  - La lista de juegos de 3DS de GameTDB (`3dstdb.zip`), que se descarga una sola vez, proporciona el año de lanzamiento para las pantallas de inicio de Virtual Console (si no se encuentra, la compilación continúa sin ese año) y los nombres del juego en otras regiones.
- Todos los títulos incluyen por defecto el sonido de inicio de Virtual Console, convertido del WAV «Wii U - Virtual Console Bootup Sound» de DaHunter54YT (128k) y empaquetado como `phase0/title_code/meta/bootSound.btsnd`. En Ayuda > Imagen y sonido se puede seleccionar un WAV propio: sustituye al sonido base en compilaciones posteriores; quitarlo restaura el predeterminado. Un sonido asignado a un juego individual tiene prioridad sobre ambos.
- Los archivos del sistema de 3DS (fuente compartida y datos Mii) no se incluyen: debes aportar los de tu propia 3DS.
  - Builder los copia desde la carpeta de usuario de Azahar o Citra, o desde cualquier carpeta que contenga una NAND de 3DS hasta tres niveles de profundidad dentro de Descargas, Escritorio o Documentos, independientemente del nombre de la carpeta. Los conserva en su carpeta de trabajo.
  - Desde una 3DS, GodMode9 permite copiar `title/0004009b` (fuente y datos Mii) y `title/000400db` (lista de palabras prohibidas) desde `[1:] SYSNAND CTRNAND`. En Builder, selecciona cualquiera de esas carpetas (o la carpeta que las contiene) con «Folder». La compilación se detiene si el código del juego solicita un archivo que no está disponible (`dumps.archives_named`): Tomodachi Life, por ejemplo, no inicia sin la lista de palabras. Azahar obtiene estos archivos mediante File > Set Up System Files y Artic Setup Tool.
  - El contenido de la NAND puede estar en formato NCCH (`<n>.app`, cifrado como en la 3DS o ya descifrado) o como imágenes RomFS extraídas (`<n>.app.romfs`). También funciona una carpeta con archivos `<title id>.romfs`.
  - Cada fila de **Tus archivos** tiene un botón **Info** que explica a los evaluadores para qué sirve ese archivo (`gui.HELP`).
  - Sin los datos Mii y la fuente compartida, ni la aplicación ni `pipeline.py` pueden compilar. Una versión de Miitopia creada sin ellos se detuvo en la consola con `[err:f] result c8804478`.
  - Si alguna compilación carece de uno de estos archivos, la pantalla de error del entorno de ejecución indica qué archivo falta.
- Durante la compilación se necesita espacio libre equivalente a unas tres veces el tamaño del juego. Los archivos de trabajo se eliminan al finalizar.
- Para compilar desde el código fuente: Python 3.10 o posterior, instalar una vez `pip install PySide6-Essentials pyctr pycryptodome` y disponer de las herramientas que utiliza `tools/win/build3ds.py`. Instala cada una una sola vez:
  - La cadena de herramientas PowerPC de devkitPro y wut, mediante `tools/win/install_toolchain.sh`, en `C:\devkitPro`.
  - LLVM, en `C:\Program Files\LLVM`.
  - MSYS2 con `mingw-w64-x86_64-gcc`, en `C:\msys64`.
  - El compilador de shaders CafeGLSL, mediante `tools/win/build_cafeglsl.ps1`.

## Cómo funciona

- La ventana utiliza Qt Quick: `ui/Main.qml` la dibuja (aspecto de cristal del menú de Wii U, distribuido en una superficie de 1280x720 que se escala a la ventana, temas claro y oscuro, preguntas dentro de la ventana y tarjeta de actualización). `ui/Simple.qml` es una ventana más sencilla a la que la tarjeta About puede cambiar y desde la que puede volver (es la misma instancia de `app`, con una sola ventana a la vez). `gui.py` mantiene el estado de la interfaz y ejecuta las acciones de sus botones. Una opción que requiere varias respuestas es un generador que presenta cada pregunta sucesivamente.
- `app.py` es el programa principal: muestra la ventana, ejecuta la compilación cuando se inicia con `--pipeline` y proporciona la parte de `python.exe` que necesitan los scripts de herramientas del repositorio. En la aplicación independiente, los compiladores se inician sin abrir ventanas de consola.
- `make_app.py` crea la aplicación independiente con PyInstaller (`python builder/make_app.py`, en un PC con las herramientas de compilación indicadas).
  - Excluye las partes de Qt que la ventana nunca carga.
  - Coloca el motor junto a la aplicación, respetando la estructura de este repositorio. Incluye también lo aprendido durante las ejecuciones de los juegos: dónde comienza su código y qué estados de renderizado se encontraron.
  - No incluye código de los juegos. Los registros de shaders de una ejecución contienen la memoria completa de shaders de cada registro de vértices, que es código propio del juego. Por eso `shaderlogs.py` distribuye cada registro como una huella digital del programa DVLB utilizado, junto al registro en un archivo `.vsf`; durante la compilación, el programa se vuelve a localizar en el volcado propio del usuario (`dvlb.bin`). En los nueve juegos comprobados, el generador de shaders produce los mismos shaders que los registros completos, salvo que declara menos entradas sin uso.
  - Si una memoria no comienza con un programa DVLB, se distribuye una receta (`.vsr`) que indica dónde están sus palabras dentro del `code.bin` y `romfs.bin` del juego, o en un archivo romfs una vez descomprimido (Yaz0, LZ10, LZ11). La compilación reconstruye el contenido desde el volcado del usuario y lo comprueba contra la huella del registro; si se trata de otra versión del juego, simplemente lo omite. Las ejecuciones cortas deben coincidir con las largas, para que una receta no pueda describir código a partir de coincidencias casuales.
    - El motor de Mario & Luigi conserva un shader como palabras sin procesar en su `code.bin` y carga después un programa desde romfs. Son 115 registros que la aplicación antes omitía y que ahora se recuperan.
    - Para un juego del que este repositorio no conserva ningún volcado, `python builder/shaderlogs.py --recipes <extracted game folder> games/g_<code>/shader_logs` genera su archivo `recipes.vsr`, que la aplicación incluye.
  - Un juego que `compat.json` marque como Complete también incluye `confirmed.bin`: todos los estados de fragmentos que se compilaron por adelantado en la versión confirmada. La compilación procesa primero esa lista, independientemente de los demás registros (solo una compilación `--cold`, que no utiliza información aprendida durante una ejecución, prescinde de ella). Sin esa lista, una versión posterior de la aplicación podría omitir algunos estados: los estados que se infieren más allá de los registros se limitan y su orden puede cambiar al añadirse un solo estado registrado (en NSMB2, un nuevo registro hizo que salieran 56 estimaciones y entraran 12). Una vez confirmado que un juego está terminado en una consola, `python builder/shaderlogs.py --confirm <that build's shader_logs> games/<slug>/shader_logs` genera la lista (una compilación de Builder conserva su carpeta con `--keep`), y `make_app.py` no crea una aplicación con un juego Complete si falta esa lista.
  - Los registros que aún se omiten se dibujan mediante la CPU en la consola. La mayoría pertenecen a otro juego, porque el registro de consola recopila información de todos los juegos: por ejemplo, NSMB2 tiene 17 que nunca llega a dibujar.
  - Para un juego sin probar compilado desde el código fuente, la compilación deja su `dvlb.bin` en la carpeta donde se guardan los registros de ejecución (`games/g_<code>/shader_logs`), para que la siguiente aplicación pueda generar sus huellas. Las herramientas de consola (`tools/console_*`), que contienen rutas de este PC y la dirección de su consola, se excluyen.
  - El proceso termina con `dist/3Dii U Builder <version> beta.zip` para los evaluadores: la carpeta de la aplicación con `Read me first.txt` (`app_readme.txt`) y `Third-party notices.txt` (`app_notices.txt`). Una nueva compilación reemplaza el ZIP anterior.
  - Escribe la cantidad de archivos de la aplicación en `_internal/files.txt`. Antes de cargar Qt, `app.py` vuelve a contarlos: si el ZIP se extrajo solo parcialmente (por ejemplo, porque «Extraer todo» de Windows se detuvo ante una ruta demasiado larga o porque un antivirus eliminó un archivo), muestra un mensaje para volver a extraerlo en una carpeta con una ruta más corta, en vez de presentar un traceback de Python. Solo se incluye el estilo Basic de Qt Quick Controls, lo que limita a 121 caracteres la ruta más profunda del ZIP.
  - La aplicación localiza ese motor, indica a `tools/win/build3ds.py` dónde están devkitPro y el generador de shaders, y compila en la carpeta del usuario (`THREEDII_ROOT`, `THREEDII_DEVKITPRO`, `THREEDII_SHADERGEN`, `THREEDII_WORK`). Al ejecutarse desde el código fuente, estas variables no se definen y todo permanece dentro del repositorio.
  - El compilador de devkitPPC no puede abrir archivos cuya ruta supere los 260 caracteres, y accede a sus encabezados mediante rutas relativas largas. Por eso la aplicación compila a través de un vínculo de directorio en `%LOCALAPPDATA%\3Dii U Builder\engine`, que apunta a la carpeta del motor incluida en la aplicación. Funciona independientemente de dónde se encuentre la aplicación y se vuelve a crear si esta se mueve.
  - La carpeta de trabajo conserva el entorno de ejecución compilado para el siguiente juego. Como los archivos fuente de una versión nueva pueden ser anteriores a lo que compiló una versión previa, cuando cambia el motor (su `stamp.txt`) se vuelve a compilar el entorno desde cero.
  - Si ocurre un error, se imprime en el registro y la compilación termina; la aplicación independiente no muestra por su cuenta un cuadro de mensaje.
- `decrypt.py` descifra primero los volcados cifrados mediante `cia_to_decrypted_3ds.py`, el conversor para el que se diseñó Builder:
  - CIA de un juego: `makerom -ciatocci` y después pyctr descifra, en el mismo lugar, todas las particiones del archivo `.3ds` generado.
  - `.3ds` cifrado: se aplica el mismo paso de pyctr sobre una copia, para no modificar nunca el volcado original.
  - CIA de una actualización: solo pyctr. makerom no puede crear una imagen de cartucho a partir de una actualización («This CIA cannot be converted to CCI»).
  - DLC: solo pyctr, contenido por contenido.
  - Las semillas de los juegos que utilizan firmware 9.6 o posterior se obtienen de `seeddb.bin`.
- `dumps.py` lee el volcado descifrado y comprueba cada parte frente a los hashes de su propio encabezado. Un descifrado incorrecto o un volcado dañado detiene la compilación antes de generar nada. Los volcados descifrados con otra herramienta que aún conservan las marcas de «cifrado» también se reconocen mediante esos mismos hashes.
- `pipeline.py` ejecuta el resto del proceso en sus propias carpetas (`builder_work/`, `build_win_builder/`):
  1. Descomprime el juego, su actualización y su DLC, y después incorpora los mods (`mods.py`: parches IPS y BPS, y un escritor RomFS que reconstruye exactamente, byte por byte, el RomFS de cada uno de los nueve juegos probados cuando no hay cambios).
  2. Recompila el código con `tools/win/build3ds.py` y compila los shaders.
  3. Ensambla el título con `tools/make_game_title.py`.
  4. Lo empaqueta con `tools/pack_game_wup.py`.
  5. Lo copia a la tarjeta, compara todos los archivos y reemplaza la copia anterior solo cuando la nueva supera las comprobaciones.

También se puede ejecutar por separado: `python builder/pipeline.py --help`.

- En el entorno de ejecución:
  - `services/fs.cpp` proporciona el RomFS de la actualización (`update_romfs.bin`, tipo de ruta SelfNCCH 5) y el del DLC (`dlc_<content index>.romfs`, archivo 0x2345678A para identificadores de título `0004008C`).
  - `services/misc.cpp` implementa las respuestas de `am:app` a las llamadas de contenido adicional, a partir de `dlc_contents.bin`, `dlc_title.bin`, `dlc_ticket.bin` y `update_title.bin`.
  - La corrección del tiempo de espera de la GPU (`services/gsp.cpp`) localiza nngxSetTimeout en cualquier código que se haya cargado, por lo que también se aplica al código actualizado y a juegos nuevos.
