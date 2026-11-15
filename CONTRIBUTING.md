# Cómo contribuir

Gracias por ayudar. Esta página explica qué instalar, cómo compilar, cómo comprobar un cambio y qué cosas nunca deben incluirse en el repositorio. [ARCHITECTURE.md](ARCHITECTURE.md) explica cómo encajan las distintas partes.

## Qué nunca debe incluirse en el repositorio

`.gitignore` funciona como una lista permitida: solo se siguen las carpetas de código fuente y todo lo demás en el nivel superior queda excluido. Mantén ese comportamiento y no agregues excepciones para ninguno de estos elementos:

| Elemento | Dónde se encuentra en tu PC | Por qué se excluye |
|---|---|---|
| Archivos de juegos: código, RomFS, ExeFS, exheader e iconos | `games/<name>/` | Son obras protegidas por derechos de autor de los editores de los juegos |
| Código C generado | `games/<name>/gen_abi/`, `gen_safe/` | Es el código del juego traducido |
| Registros de shaders con programas de vértices (`vs_log.bin`, `dvlb.bin`) | `games/<name>/shader_logs/` | Los programas de vértices son código del juego. Builder distribuye huellas digitales en su lugar (`builder/shaderlogs.py`) |
| Capturas de pantalla y hojas de contacto | `games/<name>/shots/` | Contienen imágenes del juego |
| Archivos del sistema de 3DS (fuente compartida y datos Mii) | `games/system_archives/` | Proceden de una 3DS |
| Claves: `boot9.bin`, `seeddb.bin`, `otp.bin` | En cualquier ubicación fuera del repositorio | Son secretos de tus consolas |
| Resultados de compilación: objetos, RPX, carpetas de títulos, WUP y aplicación independiente | `build*/`, `dist/`, `builder/dist/`, `builder_work/`, `phase0/build/` | Ocupan mucho espacio, se pueden volver a generar y contienen datos de juegos |
| Registros y capturas de consola | `games/<name>/console/`, `*.log` | Pueden contener resultados del juego, contenido de memoria y direcciones de tu red |
| Herramientas de consola | `tools/console_*` | Contienen la dirección de la consola y las rutas del PC de un desarrollador. Podrán volver a incluirse cuando esos datos se reciban como argumentos |
| Entorno WSL antiguo | `env/` | Contiene rutas del PC de un desarrollador y la compilación ya no lo utiliza |
| Binarios de terceros | `third_party/cafeglsl*/`, `toolchain/aac_ppc/`, `builder/bin/makerom.exe` | No son código de este proyecto. `tools/setup.py` los descarga desde una versión publicada |

Los ajustes específicos de una máquina, como las rutas y la dirección de tu consola, deben proporcionarse mediante argumentos o variables de entorno, nunca escribirse directamente en el código.

## Preparación del entorno

Necesitas Windows 10 u 11 y [Python](https://www.python.org/) 3.10 o posterior. Las contribuciones se aceptan bajo la licencia del proyecto, [EUPL-1.2](LICENSE). Nunca agregues claves, volcados de consolas ni contenido de juegos: consulta la sección «Qué nunca debe incluirse en el repositorio». Después:

1. Instala GitHub CLI e inicia sesión: `winget install GitHub.cli` y luego `gh auth login`.
2. Obtén el código: `gh repo clone J-Glitch2502/3dii-u`.
3. Dentro de esa carpeta, ejecuta `python tools/setup.py`.

El script de configuración comprueba todas las herramientas que necesita la compilación e instala las que puede:

- los paquetes de Python;
- GCC de MinGW;
- devkitPPC y wut;
- un ZIP con dependencias precompiladas, disponible en las versiones publicadas de este repositorio.

MSYS2 y LLVM tienen instaladores propios. Para ellos, el script indica qué debes instalar. Después, vuelve a ejecutarlo. La opción `--check` solo informa del estado y no instala nada.

Estas son las herramientas que quedarán instaladas:

| Herramienta | Ubicación | Versión probada | Uso |
|---|---|---|---|
| Paquetes Python: PySide6-Essentials, pyctr, pycryptodome y cryptography | | PySide6 6.12, pyctr 0.7.6 | Builder, descifrado y empaquetado de WUP |
| MSYS2 con `mingw-w64-x86_64-gcc` | `C:\\msys64` | GCC 16.2 | Instalador de la cadena de herramientas y biblioteca C para compilaciones de PC |
| LLVM | `C:\\Program Files\\LLVM` | clang 23.1 | Generador de shaders y pruebas en PC |
| devkitPPC y wut | `C:\\devkitPro`, desde `tools/win/install_toolchain.sh` | Consulta más abajo | Compilación para Wii U |
| CafeGLSL v0.2.0 para Windows | `third_party/cafeglsl/` | v0.2.0 | Compilación anticipada de shaders |
| CafeGLSL v1.0.0-rc1 para Wii U | `third_party/cafeglsl_ppc/` | v1.0.0-rc1 | Compilación de shaders faltantes en la consola |
| Decodificador AAC de FFmpeg, compilado para Wii U | `toolchain/aac_ppc/` | FFmpeg 4.4 | Juegos con música AAC y todos los juegos sin probar |
| makerom | `builder/bin/makerom.exe` | v0.19.0 | Conversión de CIA de un juego a `.3ds` antes del descifrado |
| Cemu, versión portable (solo para `check_ports.py`; debes instalarlo tú) | `build_win/cemu/` | | `tools/check_ports.py` |

**devkitPPC y wut** se instalan mediante pacman de MSYS2, pero utilizan su propia configuración, base de datos y claves dentro de `C:\\devkitPro`, por lo que no modifican la instalación de MSYS2. Las versiones probadas son devkitppc-gcc 16.2.0-1, devkitppc-binutils 2.47-2, devkitppc-newlib 4.6.0.20260123-4, devkitppc-crtls 2.1.0-1, wut 1.9.2-1, wut-tools 1.3.5-1 y general-tools 1.4.4-1. El servidor de devkitPro solo conserva los paquetes más recientes, por lo que podrías recibir versiones posteriores. Las compilaciones realizadas con ellas todavía no se han probado en una consola.

**Las dependencias precompiladas** son las cuatro herramientas que no ofrece ningún gestor de paquetes. `third_party/DEPS-NOTICES.txt` enumera sus licencias después de extraerlas. Su procedencia es la siguiente:

- **CafeGLSL v0.2.0 para Windows** se compila mediante `tools/win/build_cafeglsl.ps1`. La compilación propia de Windows del proyecto original es de una versión posterior y sus shaders de vértices dibujan incorrectamente algunas escenas.
- **La biblioteca CafeGLSL para Wii U** es la versión original v1.0.0-rc1, con las instrucciones de trampa eliminadas por `tools/win/patch_cafeglsl_traps.py`.
- **Las bibliotecas AAC** se compilan mediante `tools/aac_build.sh`, que por ahora solo funciona en WSL.
- **makerom** es la versión v0.19.0 de Project_CTR.

Para actualizar cualquiera de ellas, ejecuta `python tools/setup.py --pack` en un PC que las tenga todas. Introduce en el script el hash que imprime y sube el ZIP como una nueva versión publicada.

## Compilar un juego

Necesitas un volcado de un juego que poseas y el archivo `otp.bin` de tu Wii U. La forma más sencilla es utilizar el pipeline de Builder, que funciona con cualquier juego:

```sh
python builder/pipeline.py --game <dump> --otp <otp.bin> --out <folder>
```

Descifra y descomprime el volcado, encuentra el código, lo recompila, compila los shaders y empaqueta un WUP en `<folder>`. Las opciones `--update`, `--dlc` y `--mod` agregan esos contenidos; `--help` enumera las demás opciones. Para abrir la ventana de Builder desde el código fuente, ejecuta `builder/3Dii U Builder.pyw`.

Para trabajar en un juego paso a paso:

```sh
python tools/extract_game.py <decrypted .3ds> games/<name>
python tools/win/build3ds.py trial <name> 72000 --autoplay 3 --seed 3101
python tools/find_entries.py games/<name> --write
python tools/win/build3ds.py all <name>
python tools/win/build3ds.py pack <name> <otp.bin>
```

La prueba ejecuta el juego sin interfaz en el PC, registra el código que el recompilador no detectó y guarda los shaders que encuentra. Las capturas quedan en `games/<name>/shots/`. Un juego que carga módulos CRO también necesita `python tools/cro_entries.py games/<name>`, ejecutado con `RECOMP_CRO_BASE=0x1C000000`. Los juegos nuevos necesitan una entrada en `builder/compat.json`, la tabla única de juegos probados: slug, identificador de título, nombre, código de producto y carpeta WUP.

`build3ds.py --help` enumera todas las opciones. Las que utilizarás con más frecuencia son `--jobs N`, `--force`, `--opt -O1` y `--out <folder>`, que mantiene separada una compilación de prueba.

Para crear la aplicación independiente, que incluye Python, Qt, devkitPPC y los compiladores de shaders, primero ejecuta `pip install pyinstaller pillow`. Después:

```sh
python builder/make_app.py
```

Genera `builder/dist/3Dii U Builder/` y un ZIP para los evaluadores. Una compilación destinada a publicarse utiliza `--release <tag>`, la etiqueta de su versión de GitHub (por ejemplo, `builder-1.0-beta-2026-11-13`). La aplicación conserva esa etiqueta y la utiliza para detectar y descargar versiones nuevas (`builder/updater.py`, que enumera los archivos que cada versión debe conservar).

## Comprobar un cambio

No necesita archivos de juegos y tarda menos de un minuto. Ejecútalo antes de cada commit:

```sh
python tools/selftest.py
```

Ejecuta todas las comprobaciones de ese tipo: pequeñas pruebas en PC de las reglas del port de Wii U (`tools/test_*.cpp`), autocomprobaciones de las herramientas y pruebas de Builder (`builder/test_*.py`, `builder/selftest.py`).

Estas comprobaciones necesitan tus propios volcados y Cemu, y prueban un juego de principio a fin:

```sh
python tools/check_ports.py [game ... | all]
python tools/check_ports.py --cold [game ...]
```

Para cada juego, el primer comando compila el código, los shaders y el título, comprueba el RPX frente a los requisitos del cargador de la consola y después reproduce una secuencia de acciones mediante una copia privada de Cemu, con una partida vacía, y vuelve a iniciar el juego utilizando lo que se guardó. Un juego falla si se cierra inesperadamente, falta código, no se compiló un shader de vértices, hay un problema con la partida, se interrumpe el sonido o las imágenes difieren de una ejecución correcta. Cada ejecución recibe los Miis que tendría una consola: Miis ficticios (uno de una cuenta y dos en Mii Maker). Un juego que lee Mii Maker falla si no recibe todos: un juego que utiliza Miis debe recibir los de Wii U, así que revisa cómo los muestra. Cemu se ejecuta fuera de pantalla y no ocupa tu escritorio. La opción `--cold` compila un juego de `games/sources.json` como si fuera la primera compilación de otra persona, sin utilizar las listas de código y shaders de este PC, y lo compara con las mismas imágenes de referencia.

Ejecuta ambas comprobaciones sobre el juego en el que trabajas antes de cada commit que modifique el entorno de ejecución, el recompilador o `port/wiiu`. Revisa las imágenes que generan e indica en el commit qué pruebas ejecutaste. Cemu no tiene una GPU real y permite cosas que una consola no admite (consulta las lecciones al final de ARCHITECTURE.md), así que indica en tu pull request si también probaste el juego en una consola.

## Probar en una consola

- Instala con WUP Installer GX2. Desinstala primero cualquier versión anterior del mismo juego: instalar encima deja el sistema detenido en el banner.
- Desactiva el plugin FTP de Aroma mientras juegas. Si está activo, los juegos pueden congelarse.
- El juego se comunica con el PC mediante TCP, puerto 4405: registros, volcados de hilos y comandos de depuración (`port/wiiu/net_console.cpp`). La parte del PC es `tools/console_watch.py`, que no se incluye en el repositorio hasta que recibe la dirección de la consola como argumento.
- Las opciones de depuración son archivos vacíos en la carpeta de guardado del título (`/vol/save/common`), por ejemplo `profile` (perfil de CPU) y `noglslcompile` (desactiva la compilación de shaders en la consola).

## Enviar cambios

- Mantén cada pull request centrado en un solo tema. Describe qué cambia, por qué y cómo lo probaste.
- Conserva el comportamiento existente, salvo que el cambio tenga como objetivo modificarlo; en ese caso, indícalo.
- Escribe comentarios que expliquen por qué el código funciona de esa manera. La historia de cómo se encontró un error corresponde al mensaje del commit, no al código.
- Los cambios dentro de `third_party/3dsrecomp/` modifican el código EUPL-1.2 de nathsou y deben permanecer bajo esa licencia. Mantenlos separados de los demás cambios para que puedan proponerse al proyecto original.

## Incorporar una versión más reciente de 3dsrecomp

`third_party/3dsrecomp` es un git subtree: el código del proyecto original llegó como un único commit combinado y los cambios de este proyecto son commits normales encima. Para incorporar una versión más reciente del proyecto original:

```sh
git subtree pull --prefix=third_party/3dsrecomp https://github.com/nathsou/3dsrecomp main --squash
```

Después, compila y comprueba los ports como harías con cualquier cambio del entorno de ejecución. `git log -- third_party/3dsrecomp` muestra los cambios realizados por este proyecto.
