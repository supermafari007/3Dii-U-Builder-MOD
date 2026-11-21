# 3Dii-U Builder MOD

**3Dii-U Builder MOD es un fork modificado de 3Dii U Builder**, el proyecto original de [J-Glitch2502](https://github.com/J-Glitch2502/3dii-u). Conserva y amplía el Builder original para convertir volcados propios de juegos de Nintendo 3DS en títulos instalables en Wii U. Esta versión incluye cambios de interfaz, arte, mods y otras mejoras; consulta el historial y los archivos de este repositorio para ver el código fuente.

La aplicación recompila el código ARM del juego a C, compila ese código para la CPU PowerPC de Wii U, traduce el trabajo de la GPU de 3DS para la GPU de Wii U y empaqueta el resultado como un título que WUP Installer GX2 puede instalar en una Wii U modificada.

## Basado en

Este fork se deriva del [3Dii U Builder original](https://github.com/J-Glitch2502/3dii-u), y conserva sus avisos y licencias. También se apoya en estos proyectos de código abierto. Los avisos completos y textos de licencia se encuentran en [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

- **[3dsrecomp](https://github.com/nathsou/3dsrecomp)**, de **Nathan Soufflet (nathsou)**, EUPL-1.2: el recompilador de 3DS y el entorno de ejecución sobre los que se construye 3Dii U. Se encuentra en `third_party/3dsrecomp`, junto con las modificaciones de este proyecto para Wii U.
- **[devkitPro](https://devkitpro.org/)**: devkitPPC (GCC, Binutils, newlib), [wut](https://github.com/devkitPro/wut) y [wut-tools](https://github.com/devkitPro/wut-tools), la cadena de herramientas para Wii U.
- **[CafeGLSL](https://github.com/Exzap/CafeGLSL)**, de **Exzap**, basado en Mesa (MIT): el compilador de shaders para la GPU de Wii U.
- **[FFmpeg](https://ffmpeg.org/)** (LGPL-2.1+): su decodificador AAC, para juegos con música AAC.
- **[makerom](https://github.com/3DSGuy/Project_CTR)** de Project_CTR, de **3DSGuy, applestash y Jakcron** (MIT), y **[pyctr](https://github.com/ihaveamac/pyctr)**, de **Ian Burgwin (ihaveamac)** (MIT): lectura y descifrado de tus volcados.
- **[Qt y PySide6](https://www.qt.io/)** (LGPL-3.0), **[Python](https://www.python.org/)**, **[PyCryptodome](https://www.pycryptodome.org/)**, **[cryptography](https://cryptography.io/)** y **[PyInstaller](https://pyinstaller.org/)**: la aplicación Builder.
- **[woomïnstaller](https://github.com/shinyquagsire23/woominstaller)**, de **shinyquagsire23** (LGPL-2.1): la secuencia de arranque que incluye cada título.

La documentación también se basa en [3dbrew](https://www.3dbrew.org/) y [WiiUBrew](https://wiiubrew.org/). La estructura WUP se estudió a partir de [NUSPacker](https://github.com/Maschell/NUSPacker), de Maschell, pero no se utiliza ningún código de NUSPacker.

## Gratuito y de código abierto

3Dii U es **100 % gratuito y de código abierto**, bajo la [Licencia Pública de la Unión Europea v1.2](LICENSE). Todas las versiones son gratuitas: no hay versiones de pago, niveles beta de pago ni funciones bloqueadas tras un pago. Puedes usarlo, estudiarlo, compartirlo y modificarlo conforme a la EUPL-1.2. Si distribuyes una versión modificada, debes ofrecer su código fuente bajo la misma licencia.

**Versión de este fork: 1.0.0 MOD, solo para Windows.** [`builder/compat.json`](builder/compat.json) enumera los juegos que se han probado en una Wii U real. Cualquier otro juego se compila como «sin probar»: puede que no inicie o que se detenga a mitad de la ejecución. El paquete independiente y las futuras versiones se publican en [Releases](https://github.com/supermafari007/3Dii-U-Builder-MOD/releases).

## Lo que debes aportar

3Dii U **no incluye datos de juegos, claves ni archivos de Nintendo**. Este repositorio y la aplicación no contienen código de juegos, recursos gráficos ni código C generado. Todo proviene de tus propias consolas y de tus propios juegos, que debes volcar tú mismo:

| Archivo | Qué es | De dónde se obtiene |
|---|---|---|
| Volcados de tus juegos (`.3ds`, `.cci`, `.cia`) | Los juegos de 3DS que posees, junto con sus actualizaciones y DLC, si los tienes | Se obtienen desde tu propia 3DS, por ejemplo, con [GodMode9](https://3ds.hacks.guide/) |
| `otp.bin` | La memoria OTP (programable una sola vez) de tu Wii U. El WUP se empaqueta con la clave que contiene | Copia de seguridad de la NAND de tu propia Wii U ([Wii U Hacks Guide](https://wiiu.hacks.guide/)) |
| `boot9.bin` | La ROM de arranque ARM9 de tu 3DS. Solo se necesita para volcados cifrados | Se extrae de tu propia 3DS con GodMode9 |
| `seeddb.bin` | La base de datos de semillas de tu 3DS. Solo algunos juegos digitales la necesitan | Se extrae de tu propia 3DS con GodMode9 |
| Archivos del sistema 3DS (`0004009b` y `000400db`) | La fuente compartida, los datos Mii y la lista de palabras prohibidas de tu 3DS, que algunos juegos cargan desde la propia consola | Tu propia 3DS: GodMode9 o «Set Up System Files» de Azahar con Artic Setup Tool |

Builder comprueba cada archivo antes de utilizarlo en una compilación. Nunca muestra, almacena ni sube su contenido, y tampoco los incluye en sus paquetes: la aplicación se niega a empaquetar archivos si alguno de ellos está dentro de ella. No solicites estos archivos en el sistema de incidencias ni los publiques. Los enlaces a juegos protegidos por derechos de autor o a claves no están permitidos en ninguna parte de este proyecto.

Para jugar con lo que compiles, necesitas una Wii U con [Aroma](https://aroma.foryour.cafe/) **y su módulo sigpatches** (`01_sigpatches.rpn`). Sin ese módulo, los juegos compilados se detienen con el error 199-9999. Para instalar los títulos compilados, puedes usar [WUP Installer GX2](https://github.com/Fangal-Airbag/wup-installer-gx2) o [Wii U Transfer Tools](https://github.com/supermafari007/WiiU-Transfer-Tools), de mi repositorio.

## Cómo utilizarlo

La mayoría de los usuarios querrán **3Dii-U Builder MOD**, la aplicación de escritorio ubicada en [`builder/`](builder/README.md). Seleccionas un volcado y la aplicación compila el juego y lo coloca en tu tarjeta SD. Las versiones independientes de la aplicación no necesitan Python ni una cadena de herramientas. El botón «About» y `pipeline.py --credits` identifican este fork modificado y enumeran los proyectos en los que se basa.

## Desarrollo

- [ARCHITECTURE.md](ARCHITECTURE.md) explica cómo un volcado se convierte en un WUP y qué función cumple cada parte del código.
- [CONTRIBUTING.md](CONTRIBUTING.md) explica la cadena de herramientas, cómo compilar y cómo ejecutar las comprobaciones automáticas.

| Carpeta | Contenido |
|---|---|
| `third_party/3dsrecomp/` | Recompilador y entorno de ejecución de 3DS de nathsou (EUPL-1.2), con las modificaciones de este proyecto |
| `port/wiiu/` | La parte de Wii U: hilos, gráficos GX2, shaders, audio, partidas guardadas, controles y menú dentro del juego |
| `port/pc/` | Una compilación de Windows sin interfaz del mismo entorno de ejecución. Detecta código que el recompilador no encontró y registra los shaders que utiliza un juego |
| `tools/` | Herramientas de compilación (`tools/win/build3ds.py`), pasos de creación del título y del WUP, herramientas de shaders y utilidades para desarrolladores |
| `builder/` | 3Dii U Builder: ventana, proceso de compilación, lectura de volcados, descifrado y mods |
| `phase0/` | El primer banco de pruebas de viabilidad. El empaquetador WUP y las plantillas de títulos todavía se encuentran aquí |

## Licencia

Copyright © 2026 josiahg2502-glitch y los colaboradores de 3Dii U.

Licenciado bajo la [EUPL-1.2](LICENSE). El código de 3dsrecomp de nathsou, ubicado en `third_party/3dsrecomp`, utiliza la misma licencia y tiene su propia copia en [`third_party/3dsrecomp/LICENSE`](third_party/3dsrecomp/LICENSE). [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) contiene la información sobre todo lo demás que utiliza o distribuye 3Dii U y los requisitos de cada licencia para las versiones publicadas.

## Aviso legal

3Dii U es un proyecto independiente. **No está afiliado, respaldado ni patrocinado por Nintendo**. Nintendo 3DS, Wii U y los nombres de los juegos de Nintendo son marcas comerciales de Nintendo y se utilizan aquí únicamente para indicar con qué software es compatible este proyecto.

3Dii U está destinado exclusivamente a juegos y archivos del sistema que hayas extraído tú mismo de consolas y juegos que posees, así como a homebrew. No lo utilices con copias que no te pertenezcan. Los juegos que compila contienen código procedente de tus propios volcados, por lo que están destinados únicamente a tu propia consola: no los compartas. El software se proporciona «tal cual», sin garantía, según lo establecido en la licencia.
