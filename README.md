# 3Dii-U Builder MOD

Fork modificado de **[3Dii U Builder](https://github.com/J-Glitch2502/3dii-u)** (Windows, versión **`2.0.0 MOD`**) que recompila volcados propios de Nintendo 3DS a código PowerPC, traduce los gráficos PICA200 a GX2 y empaqueta el resultado como un título WUP instalable en Wii U.

- **Compatibilidad probada en Wii U real:** [`builder/compat.json`](builder/compat.json)
- **Descarga del ejecutable (Windows):** [Releases](https://github.com/supermafari007/3Dii-U-Builder-MOD/releases)

---

## Aportes propios de esta edición MOD

Partiendo de la base creada por el proyecto original, esta edición (`supermafari007`) incorpora desarrollo, ajustes y pruebas en consola real:

- **Interfaz y flujo de trabajo:** aplicación traducida al español, vista previa de pantallas de arranque, carpeta portable `Builder_Datos` y soporte para archivos `.cia` con caracteres Unicode.
- **Presentación en consola:** pantalla completa para TV y GamePad, filtro bilineal y soporte para sonido de arranque personalizado (`bootSound.btsnd`).
- **Multijugador y periféricos:** soporte **Pretendo Online** (*Mario Kart 7* y *Tri Force Heroes*), **LAN local (`nwm::UDS` por UDP)** entre consolas Wii U, soporte **NFC (amiibo)** y emulación de **Circle Pad Pro (`ir:USER`)** usando el stick derecho y `ZL`/`ZR`.
- **Estabilidad y compatibilidad en Wii U:** soporte para módulos `.cro.p` comprimidos (`NDCF + zlib`), bloqueo limpio de **StreetPass** (`cecd:u`/`cecd:s`), mejoras de audio/DSP (anti-bucle `nn::snd`, rampa suave anti-estática y `emergency_silence`), mejoras de video (`y2r`), política de compilación `mixed` y límite seguro de registros GPU (`48/200`) con respaldo en CPU.

---

## Requisitos y uso

### En tu Wii U
Necesitas [Aroma](https://aroma.foryour.cafe/) con el módulo **sigpatches** (`01_sigpatches.rpn`); sin él, los títulos generados se detienen con el error `199-9999`. Para instalar los paquetes WUP puedes usar [WUP Installer GX2](https://github.com/Fangal-Airbag/wup-installer-gx2) o [Wii U Transfer Tools](https://github.com/supermafari007/WiiU-Transfer-Tools).

### Archivos que debes aportar desde tus consolas
Descarga la aplicación desde [Releases](https://github.com/supermafari007/3Dii-U-Builder-MOD/releases) (o consulta [`builder/README.md`](builder/README.md) para ejecutarla desde código fuente). Para compilar, deberás aportar tus propios archivos:

| Archivo | Propósito | Cómo obtenerlo |
|---|---|---|
| Volcado del juego (`.3ds`, `.cci`, `.cia`) | Juego de 3DS de tu propiedad (y su actualización o DLC, si aplica) | Desde tu propia 3DS con [GodMode9](https://3ds.hacks.guide/) |
| `otp.bin` | Clave necesaria para empaquetar el título WUP | Respaldo de NAND de tu propia Wii U ([Wii U Hacks Guide](https://wiiu.hacks.guide/)) |
| `boot9.bin` | Requerido únicamente para volcados cifrados | Desde tu propia 3DS con GodMode9 |
| `seeddb.bin` | Requerido por algunos títulos digitales cifrados | Desde tu propia 3DS con GodMode9 |
| Archivos del sistema 3DS (`0004009b`, `000400db`) | Fuente compartida, datos Mii y listas del sistema | Desde tu propia 3DS con GodMode9 o «Set Up System Files» en Azahar |

Builder verifica estos archivos de forma local antes de compilar: no sube su contenido y rechaza empaquetar la aplicación si detecta claves o volcados en su directorio.

---

## Créditos y proyectos base

Todo el reconocimiento al proyecto original **[3Dii U Builder](https://github.com/J-Glitch2502/3dii-u)** (`J-Glitch2502` y colaboradores) y a los proyectos de código abierto sobre los que se apoya esta herramienta (textos legales completos en [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)):

- **[3dsrecomp](https://github.com/nathsou/3dsrecomp)** (Nathan Soufflet / `nathsou`, EUPL-1.2): recompilador y runtime de 3DS.
- **[devkitPro](https://devkitpro.org/)**: `devkitPPC`, [wut](https://github.com/devkitPro/wut) y [wut-tools](https://github.com/devkitPro/wut-tools).
- **[CafeGLSL](https://github.com/Exzap/CafeGLSL)** (Exzap, basado en Mesa, MIT): compilador de shaders para Wii U.
- **[FFmpeg](https://ffmpeg.org/)** (LGPL-2.1+): decodificador de audio AAC.
- **[makerom](https://github.com/3DSGuy/Project_CTR)** (Project_CTR, MIT) y **[pyctr](https://github.com/ihaveamac/pyctr)** (Ian Burgwin, MIT).
- **[Qt / PySide6](https://www.qt.io/)** (LGPL-3.0), **[Python](https://www.python.org/)**, **[PyCryptodome](https://www.pycryptodome.org/)**, **[cryptography](https://cryptography.io/)** y **[PyInstaller](https://pyinstaller.org/)**.
- **[woomïnstaller](https://github.com/shinyquagsire23/woominstaller)** (shinyquagsire23, LGPL-2.1): secuencia de arranque.

Documentación de referencia: [3dbrew](https://www.3dbrew.org/), [WiiUBrew](https://wiiubrew.org/) y el estudio de formato WUP basado en [NUSPacker](https://github.com/Maschell/NUSPacker) (sin reutilizar su código).

---

## Desarrollo

Consulta [ARCHITECTURE.md](ARCHITECTURE.md) para entender el proceso de recompilación y [CONTRIBUTING.md](CONTRIBUTING.md) para configurar el entorno de desarrollo.

| Carpeta | Contenido |
|---|---|
| `third_party/3dsrecomp/` | Recompilador y runtime de 3DS adaptado a Wii U |
| `port/wiiu/` | Implementación de Wii U (hilos, GX2, shaders, audio, guardados, controles y menú) |
| `port/pc/` | Compilación de Windows sin interfaz para descubrimiento de código y registro de shaders |
| `tools/` | Scripts de compilación (`tools/win/build3ds.py`), generación de títulos y utilidades |
| `builder/` | Aplicación de escritorio (interfaz QML, pipeline, lectura y descifrado de volcados) |
| `phase0/` | Empaquetador WUP y plantillas base de títulos |

---

## Licencia

Software **100 % gratuito y de código abierto** distribuido bajo la **[Licencia Pública de la Unión Europea v1.2 (EUPL-1.2)](LICENSE)**. No existen versiones de pago ni funciones restringidas.

- Proyecto original 3Dii U: Copyright © 2026 `josiahg2502-glitch` y colaboradores.
- Modificaciones de **3Dii-U Builder MOD**: Copyright © 2026 `supermafari007` (bajo la misma licencia EUPL-1.2).
- `third_party/3dsrecomp`: bajo EUPL-1.2 ([`third_party/3dsrecomp/LICENSE`](third_party/3dsrecomp/LICENSE)).
- Licencias de terceros: [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

---

## Política de contenido y descargo de responsabilidad

- **Solo software:** Este repositorio y sus lanzamientos contienen únicamente el código fuente y el programa compilador. **No se incluyen ni proporcionan juegos, ROMs, claves ni archivos propietarios.**
- **Sin soporte a copias de origen dudoso:** No se admite, promueve ni fomenta la piratería. Utiliza esta herramienta exclusivamente con volcados legítimos extraídos de tus propias consolas y juegos, o con software *homebrew*. No compartas los títulos compilados ni solicites juegos o claves en este repositorio.
- **Sin afiliación con Nintendo:** Proyecto independiente no afiliado, respaldado ni patrocinado por Nintendo. *Nintendo 3DS*, *Wii U* y los nombres de juegos citados son marcas registradas de sus respectivos propietarios y se mencionan solo con fines descriptivos.
- **Sin garantía («tal cual»):** El software se entrega **«tal cual» (*AS IS*)**, sin garantías de ningún tipo. Su uso y el de los títulos generados corren bajo la exclusiva responsabilidad del usuario; ni los autores originales ni el mantenedor de este fork se hacen responsables por fallos, pérdida de datos o mal uso.
