# Arquitectura

Este documento explica cómo un volcado de 3DS se convierte en un título de Wii U y dónde se encuentra cada etapa en el código. Las rutas son relativas a la raíz del repositorio. `3dsrecomp/` abrevia `third_party/3dsrecomp/`.

## De un volcado a un WUP

```
 tu volcado (.3ds .cci .cia .cxi .app)
   │ builder/decrypt.py       makerom + pyctr, con tus propias claves 3DS (solo para volcados cifrados)
   │ builder/dumps.py         comprueba cada parte con los hashes de su encabezado y luego la extrae
   │                          (tools/extract_game.py hace lo mismo desde la línea de comandos)
   ▼
 games/<name>/                exheader.bin, code.bin, romfs.bin, exefs/, info.json
   │ builder/mods.py          mods opcionales estilo Luma: archivos RomFS y parches de código IPS/BPS
   │ 3dsrecomp/tools/recomp   traduce código ARM y Thumb a C, una función C por cada función invitada
   ▼
 games/<name>/gen_abi/        juego convertido a C (nunca se confirma en Git: deriva del código del juego)
   │ devkitPPC (GCC)           compila el C del juego, el runtime y port/wiiu para la CPU de Wii U
   │ ld + elf2rpl (wut)        enlaza un ELF y lo convierte en RPX
   ▼
 <out>/<name>.rpx             más game.nm, mapa de símbolos para leer registros de consola
   │ shadergen + CafeGLSL     compila anticipadamente todos los shaders necesarios
   ▼
 <out>/shaders/               shaders.gsh (shaders compilados) y shaders.idx (clave de cada shader)
   │ tools/make_game_title.py crea las carpetas code/, content/ y meta/
   │ tools/pack_game_wup.py   cifra con la clave común de Wii U obtenida de tu otp.bin
   ▼
 dist/<folder>/               WUP: title.tmd, title.tik, title.cert y archivos .app/.h3
     → tarjeta SD → WUP Installer GX2 → menú de Wii U
```

`tools/win/build3ds.py` ejecuta las etapas intermedias (`gen`, `code`, `shaders`, `title`, `pack`). `builder/pipeline.py` ejecuta el proceso completo para Builder, en sus propias carpetas de trabajo.

## El recompilador: `3dsrecomp/tools/recomp/`

El recompilador es estático: todo el código del juego se traduce antes de ejecutarlo.

- `arm.py` y `thumb.py` decodifican instrucciones ARMv6K, incluido VFPv2, y traducen cada instrucción a C. Los registros invitados se convierten en variables locales de C y los indicadores en variables locales independientes de 0/1, para que el compilador C pueda eliminar las que nadie lee.
- `gen.py` encuentra las funciones y genera el código C. Comienza en el punto de entrada y sigue destinos `bl`, saltos de cola, punteros a código en los datos y tablas de salto. Cada llamada se convierte en una llamada C seguida de una comprobación de que la función llamada regresó al lugar esperado. Si no lo hizo, el código retrocede hasta un dispatcher que reanuda la ejecución en la instrucción correcta.
- `cro.py` gestiona módulos CRO, código reubicable que algunos juegos cargan desde su RomFS. Cada módulo recibe un espacio fijo para que el código recompilado conozca sus direcciones. La compilación de Wii U coloca esos espacios en `0x1C000000` (`RECOMP_CRO_BASE`), dentro de los 512 MB disponibles para el invitado.

Este proyecto añade el modo `--abi` (salida `gen_abi`). Confía en la convención de llamadas ARM: r4-r11, sp y s16-s31 se conservan entre llamadas, por lo que permanecen en variables locales de C y solo los registros de argumentos y retorno pasan por memoria. Esto reduce el código aproximadamente un 30 %. El código escrito manualmente que incumple la convención (ayudantes soft-float y decodificadores de vídeo) se detecta, y los puntos de llamada sincronizan todos los registros necesarios. Los juegos cuyo código incumple la convención de formas que no pueden detectarse se compilan en el modo seguro anterior (`gen_safe`): todos los registros pasan por memoria en cada llamada. `tools/win/build3ds.py` selecciona el modo a partir del propio código del juego:

- Ocho o más saltos de tabla de desplazamientos de Mobiclip: su decodificador de vídeo conserva valores en cualquier registro entre sus propias llamadas.
- Una llamada a setjmp seguida, pocas instrucciones después, de una llamada a longjmp: tareas que cambian de pila, como los hilos de script de Ace Attorney Trilogy. Su longjmp aterriza en un marco que el código C ya abandonó. El modo seguro tiene un punto de reanudación en cada lugar de retorno, para que el dispatcher pueda continuar allí.

**Código que el análisis no detectó.** En un PC, un salto a código que no se recompiló se compila en el momento y se registra en `functions.txt`. La consola no puede hacerlo, por lo que el código omitido provoca un cierre inesperado. Antes de la primera compilación para consola, cada juego pasa por estos pasos:

- `tools/find_entries.py` y `tools/cro_entries.py` buscan código al que solo se llega de formas escritas manualmente, como valores `lr` calculados y llamadas mediante tablas.
- Una prueba en PC (`build3ds.py trial`) ejecuta el juego sin interfaz y agrega a `functions.txt` el código que alcanza.

## Memoria invitada y CPU

- El mapa de direcciones de 3DS cabe por debajo de `0x20000000`. En Wii U, `3dsrecomp/runtime/core.cpp` asigna un bloque plano de `0x20010000` bytes. Los sistemas anfitriones de 64 bits reservan 4 GiB.
- La memoria invitada sigue siendo little-endian, como en 3DS. [`port/cpu.h`](port/cpu.h) reemplaza las macros de memoria en la Wii U big-endian por cargas y escrituras con inversión de bytes (`lwbrx` y equivalentes). Utilizan base más índice, por lo que un acceso invitado cuesta una instrucción, igual que uno nativo.
- El C generado se compila con `-ffp-contract=off` y `-fno-strict-aliasing`, para que los resultados de punto flotante coincidan con 3DS y los casts de la traducción funcionen correctamente.

## El entorno de ejecución: `3dsrecomp/runtime/`

El runtime reemplaza el sistema operativo y el hardware de 3DS. Las mismas fuentes se compilan para Wii U y PC.

- `core.cpp`: memoria invitada, dispatcher y registro. En consola, el registro permanece en memoria y se envía al PC por red (consulta `net_console.cpp`).
- `kernel.cpp`: hilos, eventos, mutexes, semáforos, temporizadores, arbitraje de direcciones, memoria compartida e interfaz SVC. Cada hilo invitado es un hilo anfitrión, pero solo se ejecuta uno a la vez, con planificación estricta por prioridad como en el núcleo de aplicaciones de 3DS.
- `service.cpp` y `services/`: servicios del sistema con los que el juego se comunica por IPC: srv, APT, fs (RomFS, partidas y RomFS de actualizaciones o DLC), gsp (cola de comandos de GPU, framebuffers y registros LCD), hid (botones, pantalla táctil y entrada por scripts), cfg, dsp, mic, cam, y2r, am:app (contenido adicional) y ldr:ro (módulos CRO). Los demás son stubs.
- `gpu/`: PICA200. Decodifica registros en estados de dibujo, interpreta programas de vértices, decodifica texturas y realiza transferencias de pantalla. Su rasterizador por software es el renderizador de referencia y lo utiliza la compilación para PC.
- `audio/`: implementación HLE del firmware de audio del DSP. Los flujos AAC se descodifican mediante FFmpeg (`port/aac/aac_ffmpeg.cpp`).

Los archivos `.mm`, `frontend.cpp`, `main.cpp` y `audio/host_sdl.cpp` pertenecen al frontend original de macOS e iOS, al renderizador Metal y a la salida de audio SDL. Las compilaciones de Wii U y PC no los incluyen.

## Gráficos en Wii U: `port/wiiu/gx2_backend.cpp`

El backend divide el trabajo como el renderizador Metal original. `runtime/gpu` decodifica los registros de PICA200 en un `DrawState`, y el backend convierte cada dibujo en estados GX2 y una llamada de dibujo.

**Los shaders se compilan por adelantado.** No se compila nada cuando el juego necesita un shader por primera vez, por lo que no hay interrupciones.

- [`shadergen.cpp`](port/wiiu/shadergen.cpp) genera GLSL. Un shader de fragmentos cubre un estado de la canalización de fragmentos: combinadores TEV, iluminación de fragmentos, niebla y prueba alfa. Un shader de vértices cubre un programa de vértices PICA o un programa de vértices seguido de un shader de geometría. Cada ejecución de este último genera un vértice (partículas) o los vértices de un primitivo (modo de primitivos fijos: la lluvia de Luigi's Mansion 2) mediante unos pocos triángulos: cada vértice dibujado es una esquina de un triángulo que emite, con las entradas de los vértices del juego y el número de esa esquina (`gen_gsx_glsl`).
- [`shaderkeys.cpp`](port/wiiu/shaderkeys.cpp) define las claves que identifican cada shader. El generador del PC y el renderizador de la consola compilan el mismo código, por lo que calculan la misma clave.
- [`tools/shadergen_main.cpp`](tools/shadergen_main.cpp) genera todos los shaders necesarios. CafeGLSL v0.2.0 (`third_party/cafeglsl/glslcompiler.exe`) los compila y `build3ds.py` combina los resultados en `shaders.gsh`.
- Los shaders proceden de varias fuentes:
  - Registros de la prueba en PC (`vs_log.bin`, `fs_log.bin`).
  - Todos los programas de vértices encontrados en los archivos del juego (`tools/dvlb_to_log.py`): binarios de shaders y listas de comandos de GPU que los archivos de modelos de Game Freak utilizan para cargar shaders (`tools/shadergen_lists.cpp`).
  - Todos los materiales de los archivos de modelos (`tools/tev_to_log.py`).
  - Registros de otros juegos, porque los estados de fragmentos no dependen del juego.
  - Ausencias informadas por las consolas (`tools/misses_to_log.py`).

**Cuando falta un shader**, un estado de fragmentos se dibuja con un shader «uber» genérico o con una alternativa sencilla, y se registra. Un programa de vértices sin shader se procesa en la CPU, al igual que el dibujo de un shader de geometría (se registra para la siguiente compilación o se deja allí si su resultado podría depender del estado de una ejecución anterior). La consola también puede compilar los shaders de píxeles faltantes en un hilo propio, mediante la biblioteca CafeGLSL para Wii U ([`shader_compile.cpp`](port/wiiu/shader_compile.cpp)). El GLSL de cada ausencia se conserva en la carpeta de guardado, de modo que en el siguiente inicio se compile antes de necesitarlo. Un archivo llamado `noglslcompile` en la carpeta de datos desactiva esta función.

El resto del backend, en el orden de las secciones del archivo:

- superficies y destinos de renderizado;
- texturas;
- estado de renderizado;
- dibujos con un shader de vértices compilado;
- dibujos procesados por la CPU;
- lectura de los resultados de la GPU de vuelta a la memoria invitada;
- presentación: la pantalla superior se dibuja a 1200x720 (800x480 en el menú a 480p) y cada pantalla se escala según la distribución del televisor y del GamePad (`screen_layouts.h`);
- menú de opciones, con su propia fuente pequeña;
- teclado por software.

## El frontend de Wii U: `port/wiiu/`

| Archivo | Función |
|---|---|
| `frontend_wiiu.cpp` | Inicio, hilos y núcleos, VBlank, ProcUI y menú HOME, entrada del GamePad y watchdog que muestra la etapa de inicio y las direcciones de los hilos si no se dibuja nada |
| `audio_wiiu.cpp` | Dos voces AX en bucle que reproducen la mezcla HLE del DSP |
| `save_storage.cpp` | Partidas en el almacenamiento propio del título en la consola, mediante `nn_save` |
| `fsa_direct.cpp`, `direct_read.cpp` | Llamadas a archivos que omiten los hooks ContentRedirection de Aroma |
| `net_console.cpp` | Registro, volcados de hilos y comandos de depuración por TCP, puerto 4405 |
| `console_profile.cpp` | Perfilador de muestreo para consola real: se activa con un archivo vacío `profile` en la carpeta de guardado |
| `mic_wiiu.cpp` | Micrófono del GamePad para mic:u. Mantener pulsado el stick derecho equivale a soplar |
| `stubs.cpp`, `shim.h`, `TargetConditionals.h`, `mach/` | Sustitutos de las llamadas de macOS utilizadas por el runtime original |

Los hilos invitados comparten el núcleo 1, que tiene la caché L2 de 2 MB. El trabajo de la GPU se ejecuta en el núcleo 2, y la entrada y el audio en el núcleo 0.

### Los Miis de la consola

Un juego que utiliza Miis recibe los de la propia Wii U. Es una regla para todos los juegos: la transferencia utiliza código compartido y no se configura individualmente.

- Al iniciar, `frontend_wiiu.cpp` lee el Mii de cada cuenta (`nn::act`) y después la base de datos de Mii Maker (`FFL_ODB.dat`, en sus datos guardados).
  - La base de datos todavía no está confirmada en una consola: no se ha comprobado si la consola permite que un juego abra los datos guardados de Mii Maker ni si es correcto el formato del archivo, tomado del de 3DS. Si no se puede abrir, el juego recibe únicamente los Miis de las cuentas. Solo se ocupa una ranura si su contenido parece ser un Mii.
- `mii_3ds()` en `services/misc.cpp` convierte cada Mii al formato que lee un juego de 3DS:
  - Puede venir en el formato en que lo conserva una 3DS o en el formato que lee el procesador de Wii U, cuyos campos están en orden inverso. El segundo formato se invierte por completo.
  - Un Mii creado en una consola más nueva que 3DS se presenta como si fuera de una 3DS. Super Mario 3D Land, de 2011, no acepta otro tipo: mostró «You don't have any Mii characters» y el archivo de guardado perdió el rostro al siguiente inicio.
- Se introducen en la base de datos Mii Maker propia del juego, con un máximo de 100 (`sync_mii_db()` en `services/fs.cpp`), donde los juegos que enumeran Miis pueden encontrarlos.
  - Un Mii conserva el lugar que ocupaba en un inicio anterior; uno nuevo utiliza el primer lugar libre y cada uno indica la posición en la que está guardado. Una 3DS conserva un Mii en la posición que este indica y los juegos vuelven a encontrarlo por ese dato: 3D Land no mostró ningún rostro en una partida cuyo Mii indicaba una posición ocupada por otro Mii.
- El Mii de la cuenta actual es el del jugador. El servicio de amigos y el applet selector de Mii lo entregan indicando la misma posición que su copia en la base de datos, donde se coloca primero y siempre tiene una posición asignada. El selector responde inmediatamente y no muestra opciones.
- `tools/check_ports.py` proporciona a cada ejecución un Mii de cuenta y una pequeña base de datos Mii Maker ficticia, con Miis en ambos formatos y una ranura vacía. Una ejecución falla si la base de datos Mii Maker del juego termina teniendo más o menos Miis que los esperados. El Mii de cuenta de Cemu es un marcador provisional, por lo que sin estos datos la prueba simularía una consola sin ningún Mii.

## La compilación para PC: `port/pc/`

`frontend_pc.cpp` ejecuta el mismo runtime sin interfaz en Windows, con el rasterizador por software. Reproduce los hilos y la cadencia de VBlank de la consola. La entrada procede de un script (`RECOMP_AUTOPLAY` o líneas de un archivo `live.txt`) y ambas pantallas se guardan como imágenes. `build3ds.py trial` lo compila con LLVM y MinGW y ejecuta un juego durante una cantidad determinada de fotogramas. Una prueba realiza tres tareas:

- registra el código que el recompilador no detectó, en `functions.txt`;
- registra los programas de vértices y estados de fragmentos que encuentra (`vs_log.cpp`);
- deja una hoja de contacto con capturas para ver de un vistazo hasta dónde llegó el juego.

## La compilación: `tools/win/build3ds.py`

| Comando | Función |
|---|---|
| `gen <game>` | Vuelve a generar en C el código ARM del juego |
| `code <game>` | Runtime, C del juego y enlace: RPX y `game.nm` |
| `shaders <game>` | Paquete de shaders |
| `title <game>` | Carpeta del título |
| `all <game>` | Ejecuta `code`, `shaders` y `title` |
| `pack <game> <otp.bin>` | Genera el WUP |
| `trial <game> [frames]` | Prueba del juego en PC |

Las compilaciones se ejecutan con prioridad inferior a la normal, como máximo `--jobs` a la vez (4 de forma predeterminada), y ninguna se inicia si el PC tiene poca memoria libre. Los objetos se guardan en `build_win/`. Solo se recompila lo que cambió: cada objeto recuerda su línea de comandos y los archivos de dependencias del compilador cubren los encabezados.

Los juegos pertenecen a tres familias, con algunas opciones distintas (`Game` en `build3ds.py`):

- Tomodachi Life (`build/`).
- Los juegos de Mario y los juegos sin probar de Builder (`build_mario/<game>/`).
- Los juegos de Layton (`build_layton/<game>/`, modo seguro en `-O1`).

## Títulos y WUP

`tools/make_game_title.py` ensambla tres carpetas:

- `code/`: RPX, `app.xml` y `cos.xml`. El heap de código (`max_codesize`) se establece en el máximo, 224 MB, porque también contiene las bibliotecas del sistema.
- `content/`: `exheader.bin`, `code.bin` y `romfs.bin` del juego (enlaces duros, no copias), su icono, los archivos del sistema de 3DS y `shaders.gsh` y `shaders.idx`. Aquí también va el RomFS de una actualización o DLC.
- `meta/`: `meta.xml` (identificador de título, nombres y tamaños de guardado), iconos e imágenes de inicio que dibuja por sí mismo y `bootMovie.h264`.

Las plantillas están en `phase0/title_code/`, y `tools/make_game_title.py` crea cada título a partir de ellas. Los juegos probados conservan sus identificadores y nombres en `builder/compat.json`. Los juegos sin probar reciben `000500003Dxxxxxx`, reservado para evitar conflictos con títulos reales de Wii U.

`tools/pack_game_wup.py` lee la clave común de Wii U desde tu `otp.bin` y la entrega a `phase0/tools/pack_wup.py` mediante stdin, por lo que nunca se imprime ni se almacena. El empaquetador escribe el TMD, el ticket, los certificados y los contenidos cifrados con sus árboles de hashes. Después descifra todo lo que escribió y lo compara con la entrada.

## Builder: `builder/`

- `app.py` es el programa único. Ejecuta la ventana, la compilación cuando se inicia con `--pipeline` y la parte de Python que necesitan los scripts de herramientas.
- `gui.py` y `ui/Main.qml` conforman la ventana: Qt Quick, con el estado y las acciones en Python.
- `pipeline.py` recorre las etapas: lectura, descifrado, extracción, búsqueda de código, recompilación, shaders, título, empaquetado y copia. Imprime líneas `@stage`, `@progress`, `@done` y `@error` para que la ventana siga el proceso.
- `dumps.py` lee contenedores CIA, NCSD y NCCH y comprueba cada parte con los hashes de su encabezado. `decrypt.py` y `cia_to_decrypted_3ds.py` descifran.
- `mods.py` aplica mods al estilo Luma. Su escritor RomFS reconstruye el RomFS de cada juego probado byte por byte cuando no hay cambios.
- `shaderlogs.py` reemplaza el código de programas de vértices de los registros de shaders por huellas digitales, porque ese código pertenece al juego. La compilación vuelve a encontrar cada programa en el volcado del usuario. Una memoria de shader que no comienza con un programa DVLB se distribuye como una receta: indica dónde están sus palabras en el code.bin y romfs del juego, y se comprueba contra la huella cuando se reconstruye.
- `make_app.py` crea la aplicación independiente con PyInstaller. La aplicación incluye una carpeta `engine/`: fuentes del recompilador y del runtime, scripts de compilación, devkitPPC y compiladores de shaders.

[`builder/README.md`](builder/README.md) contiene los detalles.

## Lecciones del hardware real

Cemu es útil para comprobaciones rápidas, pero se comporta de forma distinta a una consola en aspectos importantes:

- **Carga.** Cemu carga archivos RPX que la consola rechaza. El error 160-2203 significa que el cargador de la consola se detuvo antes de `main`. [`tools/rpx_check.py`](tools/rpx_check.py) ejecuta las comprobaciones del cargador que pueden realizarse sin consola. Nunca elimines las reubicaciones del RPX: la consola rechaza incluso un programa diminuto si no las tiene.
- **Enlace de código grande.** Por encima de 32 MB de `.text`, `ld --relax` coloca stubs de saltos lejanos en `.init` del runtime C. El flujo continúa hacia ellos y entra en un bucle antes de `main`. El script de enlace mantiene juntos `.init`, `.fini` y el código crt.
- **Trampas.** Cemu detiene un hilo ante cualquier instrucción de trampa condicional. El runtime se compila con `-fno-if-conversion`, y `tools/win/patch_cafeglsl_traps.py` elimina las cinco trampas de la biblioteca CafeGLSL para Wii U.
- **Patrones del compilador.** Cemu ejecuta incorrectamente una comparación guardada a través de una llamada con `mfcr`/`mtcrf` cuando la rama posterior comprueba otro bit de condición. Si Cemu y la consola no coinciden, comprueba primero este caso.
- **Temporización de GPU.** Una GPU real comienza un dibujo cuando GX2 vacía la cola, bastante después de que se envíe; Cemu lo inicia inmediatamente. Los vértices se leen directamente de la memoria invitada, que el juego vuelve a escribir en cuanto sabe que una lista terminó. Por eso el backend espera a la GPU antes de informar que una lista se completó.
- **Memoria sin inicializar.** La memoria de Cemu comienza en cero y la de una consola no. Los destinos de renderizado parten de lo que el juego dejó en la memoria invitada, como en una 3DS. `-DGX2_JUNK=7` llena las asignaciones nuevas con ruido, para que Cemu muestre lo que ocurriría en una consola.
- **Registros de shaders.** `GX2SetShaderMode` deja 48 registros para shaders de vértices y 200 para shaders de píxeles. Un shader que necesita más no puede iniciarse en una GPU real, aunque Cemu no tenga problemas. `build3ds.py` comprueba cada paquete de shaders.
- **Esperas condicionales.** La espera condicional temporizada de wut configura una alarma y luego duerme; si la alarma se dispara en medio, se pierde la activación. Utiliza `OSWaitEventWithTimeout`.
- **Archivos y Aroma.** ContentRedirection de Aroma intercepta todas las llamadas a archivos de todos los títulos y asigna memoria desde un heap compartido por Aroma bajo un único spin lock. Con el plugin FTP manteniendo ese bloqueo, los juegos se congelaban. Por eso las llamadas a archivos omiten esos hooks y ningún registro toca la tarjeta SD mientras se ejecuta un juego. Mantén desactivado el plugin FTP de Aroma mientras juegas.
- **Menú HOME.** ProcUI mantiene una lista de callbacks por núcleo. El callback de liberación debe ejecutarse en el núcleo de la GPU y con mayor prioridad; de lo contrario, compite con el callback de WHB mientras el juego todavía está dibujando.
- **Partidas guardadas.** Las escrituras solo se conservan después de vaciarlas (`SAVEFlushQuota`). La consola no permite buscar más allá del final de un archivo, por lo que los archivos crecen escribiendo ceros.
- **Instalación.** Instalar una compilación sobre otra versión del mismo título deja el sistema detenido en el banner, dentro del cargador y antes de ejecutar código del juego. Desinstala primero la versión antigua.
- **Importaciones.** Cada biblioteca del sistema que importa un RPX es una cosa más que el cargador debe preparar. Las bibliotecas que el juego solo necesita en ocasiones (sockets, micrófono y partidas guardadas) se obtienen mediante `OSDynLoad`.
- **Temporización de 3DS.** La espera de GPU del juego tiene un tiempo límite (`nngxSetTimeout`) que un anfitrión ocupado puede alcanzar, pero una 3DS no; por eso el runtime lo mantiene en cero. La implementación HLE del DSP nunca inicia un fotograma antes de que el juego haya terminado de utilizar el anterior.
- **Los números de fotograma del PC no representan tiempo real.** Bajo carga, la misma pantalla puede aparecer muchos fotogramas más tarde; por eso una ejecución en PC debe evaluarse por sus imágenes.
