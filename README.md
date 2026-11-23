# 3Dii-U Builder MOD

> **Aviso importante:** Este proyecto es un **fork modificado** basado en el proyecto original **[3Dii U Builder](https://github.com/J-Glitch2502/3dii-u)** de **J-Glitch2502**.  
> Este repositorio y sus lanzamientos contienen **únicamente el programa y su código fuente**: **no se incluyen ni se proporcionan juegos, ROMs, archivos de sistema ni claves de consola**, y **no se admite, respalda ni fomenta el uso de copias de origen dudoso**.

**3Dii-U Builder MOD** recompila código ARM de volcados propios de Nintendo 3DS a C, lo compila para la CPU PowerPC de Wii U, traduce el trabajo gráfico de PICA200 a GX2 y empaqueta el resultado como un título instalable en una Wii U con Aroma.

**Versión actual de este fork:** `2.0.0 MOD` (Windows).  
El listado de juegos probados en consola real está en [`builder/compat.json`](builder/compat.json) y los paquetes independientes se publican en [Releases](https://github.com/supermafari007/3Dii-U-Builder-MOD/releases).

---

## Autoría original y trabajo propio en esta versión MOD

Este repositorio reconoce plenamente la autoría y el trabajo base del **[proyecto original 3Dii U Builder](https://github.com/J-Glitch2502/3dii-u)** (`josiahg2502-glitch` y colaboradores), así como de todos los proyectos de código abierto sobre los que se construye (ver sección *Basado en* y [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)).

Sobre esa base, **3Dii-U Builder MOD** incorpora desarrollo, pruebas en Wii U real, parches y características propias realizadas para esta edición modificada (`supermafari007`), entre ellas:

- **Interfaz y experiencia en español:** traducción completa de la aplicación Builder, vistas previas de arranque y flujo portable con carpeta `Builder_Datos`.
- **Presentación en consola:** pantalla completa para TV y GamePad, filtro bilineal y soporte para sonido de arranque personalizado (`bootSound.btsnd`).
- **Multijugador y periféricos:** soporte **Pretendo Online** (*Mario Kart 7* y *The Legend of Zelda: Tri Force Heroes*), modo **LAN local (`nwm::UDS` por UDP)** entre consolas Wii U, soporte **NFC (amiibo)** y emulación de **Circle Pad Pro (`ir:USER`)** con stick derecho y `ZL`/`ZR`.
- **Compatibilidad y estabilidad en Wii U real:** soporte para módulos `.cro.p` comprimidos (`NDCF + zlib`), soporte para archivos `.cia` con caracteres Unicode, bloqueo limpio de **StreetPass** (`cecd:u`/`cecd:s`) para evitar congelamientos, mejoras anti-bucle/anti-estática en audio y DSP (`nn::snd`, rampa suave en `audio_wiiu.cpp`, `emergency_silence`), mejoras de video (`y2r`), política `mixed` y límite seguro de registros GPU (`48/200`) con respaldo en CPU.

---

## Política de contenido (solo software, sin juegos ni datos)

Este proyecto **es exclusivamente una herramienta de software**. Ni el repositorio ni las versiones compiladas contienen juegos, ROMs, archivos `.3ds`/`.cci`/`.cia`, claves criptográficas, firmware ni activos protegidos por derechos de autor.

- **No se proporcionan juegos ni enlaces de descarga.**
- **No se admite ni se fomenta la piratería ni el uso de copias de origen dudoso.**
- El programa está diseñado **únicamente** para funcionar con volcados legítimos que hayas extraído tú mismo de tus propias consolas y juegos físicos o digitales, así como con software *homebrew*.
- No solicites ni compartas juegos, claves ni archivos de sistema en los *issues* o discusiones; cualquier contenido de ese tipo será rechazado.

### Lo que debes aportar desde tus propias consolas

| Archivo | Qué es | De dónde se obtiene |
|---|---|---|
| Volcados de tus juegos (`.3ds`, `.cci`, `.cia`) | Los juegos de 3DS que posees (y sus actualizaciones o DLC, si los tienes) | Extraídos desde tu propia 3DS, por ejemplo, con [GodMode9](https://3ds.hacks.guide/) |
| `otp.bin` | Memoria OTP de tu Wii U necesaria para empaquetar el WUP | Copia de seguridad de la NAND de tu propia Wii U ([Wii U Hacks Guide](https://wiiu.hacks.guide/)) |
| `boot9.bin` | ROM de arranque ARM9 de tu 3DS (solo para volcados cifrados) | Extraída desde tu propia 3DS con GodMode9 |
| `seeddb.bin` | Base de datos de semillas de tu 3DS (solo para ciertos títulos digitales) | Extraída desde tu propia 3DS con GodMode9 |
| Archivos del sistema 3DS (`0004009b` y `000400db`) | Fuente compartida, datos Mii y lista de palabras del sistema que requieren algunos juegos | Extraídos desde tu propia 3DS con GodMode9 o mediante «Set Up System Files» en Azahar |

Builder valida estos archivos localmente antes de compilar: nunca sube ni comparte su contenido y rechaza empaquetar la aplicación si detecta claves o volcados dentro de su carpeta.

---

## Requisitos en Wii U y uso

Para instalar y ejecutar los títulos generados necesitas una Wii U con [Aroma](https://aroma.foryour.cafe/) y su módulo **sigpatches** (`01_sigpatches.rpn`); sin ese módulo, los títulos compilados se detendrán con el error `199-9999`. Para instalar los WUP puedes utilizar [WUP Installer GX2](https://github.com/Fangal-Airbag/wup-installer-gx2) o [Wii U Transfer Tools](https://github.com/supermafari007/WiiU-Transfer-Tools).

La forma más sencilla de empezar es usar la aplicación de escritorio **3Dii-U Builder MOD** (ver [`builder/`](builder/README.md) o descargar el paquete independiente en [Releases](https://github.com/supermafari007/3Dii-U-Builder-MOD/releases)): seleccionas tu volcado y el programa compila y prepara el título listo para tu tarjeta SD.

---

## Basado en

Este fork deriva del [3Dii U Builder original](https://github.com/J-Glitch2502/3dii-u) y conserva íntegramente sus créditos y licencias. También utiliza los siguientes proyectos de código abierto (avisos y textos completos en [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)):

- **[3dsrecomp](https://github.com/nathsou/3dsrecomp)**, de **Nathan Soufflet (nathsou)** (EUPL-1.2): recompilador y entorno de ejecución de 3DS (`third_party/3dsrecomp`).
- **[devkitPro](https://devkitpro.org/)**: devkitPPC (GCC, Binutils, newlib), [wut](https://github.com/devkitPro/wut) y [wut-tools](https://github.com/devkitPro/wut-tools).
- **[CafeGLSL](https://github.com/Exzap/CafeGLSL)**, de **Exzap**, basado en Mesa (MIT): compilador de shaders para la GPU Latte de Wii U.
- **[FFmpeg](https://ffmpeg.org/)** (LGPL-2.1+): decodificador AAC.
- **[makerom](https://github.com/3DSGuy/Project_CTR)** (Project_CTR, de **3DSGuy, applestash y Jakcron**, MIT) y **[pyctr](https://github.com/ihaveamac/pyctr)** (de **Ian Burgwin / ihaveamac**, MIT).
- **[Qt y PySide6](https://www.qt.io/)** (LGPL-3.0), **[Python](https://www.python.org/)**, **[PyCryptodome](https://www.pycryptodome.org/)**, **[cryptography](https://cryptography.io/)** y **[PyInstaller](https://pyinstaller.org/)**.
- **[woomïnstaller](https://github.com/shinyquagsire23/woominstaller)**, de **shinyquagsire23** (LGPL-2.1): secuencia de arranque de los títulos.

La documentación técnica se apoya además en [3dbrew](https://www.3dbrew.org/) y [WiiUBrew](https://wiiubrew.org/). La estructura WUP se estudió a partir de [NUSPacker](https://github.com/Maschell/NUSPacker), sin reutilizar código de ese proyecto.

---

## Desarrollo y estructura del repositorio

- [ARCHITECTURE.md](ARCHITECTURE.md): explica cómo un volcado se convierte en un WUP y el papel de cada componente.
- [CONTRIBUTING.md](CONTRIBUTING.md): detalla la cadena de herramientas, compilación y pruebas.

| Carpeta | Contenido |
|---|---|
| `third_party/3dsrecomp/` | Recompilador y runtime de 3DS de nathsou (EUPL-1.2), con las adaptaciones para Wii U y mejoras de este fork |
| `port/wiiu/` | Implementación de Wii U: hilos, gráficos GX2, shaders, audio, guardados, controles y menú en juego |
| `port/pc/` | Compilación de Windows sin interfaz para descubrimiento de código y registro de shaders |
| `tools/` | Scripts de compilación (`tools/win/build3ds.py`), creación de títulos/WUP y utilidades |
| `builder/` | Aplicación de escritorio 3Dii-U Builder MOD: interfaz QML, pipeline, lectura de volcados y descifrado |
| `phase0/` | Empaquetador WUP y plantillas base de títulos |

---

## Licencia

Este proyecto es **100 % gratuito y de código abierto**, bajo la **[Licencia Pública de la Unión Europea v1.2 (EUPL-1.2)](LICENSE)**. No existen versiones de pago ni funciones bloqueadas tras suscripciones.

- Código original de 3Dii U: Copyright © 2026 josiahg2502-glitch y colaboradores de 3Dii U.
- Modificaciones y ampliaciones de **3Dii-U Builder MOD**: Copyright © 2026 supermafari007 (distribuidas bajo la misma licencia EUPL-1.2).
- `third_party/3dsrecomp`: bajo EUPL-1.2 (ver [`third_party/3dsrecomp/LICENSE`](third_party/3dsrecomp/LICENSE)).
- Terceros: ver [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

---

## Descargo de responsabilidad y aviso legal

1. **Sin afiliación con Nintendo:** 3Dii-U Builder MOD es un proyecto independiente de código abierto y **no está afiliado, respaldado, patrocinado ni aprobado por Nintendo**. *Nintendo 3DS*, *Wii U* y todos los nombres de juegos o servicios mencionados son marcas registradas de sus respectivos propietarios y se citan únicamente con fines descriptivos y de compatibilidad.
2. **Uso legal y personal:** Este software está destinado **exclusivamente** al uso personal con copias de seguridad legítimas de juegos y archivos de sistema extraídos de tus propias consolas, o con software *homebrew*. Los títulos compilados contienen código procedente de tus propios volcados y están pensados solo para tu consola: **no los redistribuyas**.
3. **Sin garantía («tal cual»):** Este programa se distribuye **«tal cual» (*AS IS*)**, sin garantía expresa ni implícita de ningún tipo (incluyendo estabilidad, compatibilidad total o ausencia de errores). El uso de esta herramienta y de los títulos generados en tu consola corre **bajo tu exclusiva responsabilidad**; ni los autores del proyecto original ni el mantenedor de este fork asumen responsabilidad por daños, pérdida de datos, bloqueos o mal uso del software.
