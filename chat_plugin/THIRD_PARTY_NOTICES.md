# Interpretwitch dependencies

Interpretwitch source is GPL-2.0-or-later. This development package dynamically
uses OBS's Qt 6.11.1 libraries and includes Qt WebSockets 6.11.1. Applicable Qt
license texts, including LGPL-3.0, are included in `licenses/`. Corresponding Qt
WebSockets source is in `source.zip`; its official source and the matching OBS Qt
SDK are pinned in `dependencies.lock.json`. The shared library can be rebuilt and
replaced. Re-sign a modified bundle locally before loading it into OBS.

The existing-dock adapter includes the GPL-2.0-or-later OBS Browser
panel interface header, pinned to the OBS 32.2.2 submodule revision. See
`third_party/obs-browser/README.md` in the source archive. It uses the browser
already installed with OBS; no CEF/browser runtime is bundled.

- **CTranslate2 4.8.2**, OpenNMT, MIT. The Mac library is extracted from the
  official pinned PyPI wheel; Python is not required by the installed plugin.
  Source: https://github.com/OpenNMT/CTranslate2/tree/v4.8.2
- **SentencePiece 0.2.0**, Google, Apache-2.0. Statically linked, including its
  bundled minimal Abseil/protobuf support. Source and notices are in `source.zip`.
  https://github.com/google/sentencepiece/tree/v0.2.0
- **fastText 0.9.2**, Facebook, MIT. Statically linked language detector.
  https://github.com/facebookresearch/fastText/tree/v0.9.2
- **SIMDe**, MIT, header-only SIMD compatibility used by the OBS headers on Mac.
  Pinned to commit `c3d7abfaba6729a8b11d09a314b34a4db628911d`; source and copyright
  notice are included in the package. No additional runtime library.
  https://github.com/simd-everywhere/simde/tree/c3d7abfaba6729a8b11d09a314b34a4db628911d
- **Qt WebSockets 6.11.1**, The Qt Company, used under LGPL-3.0 with applicable
  exceptions/notices from the source archive. https://github.com/qt/qtwebsockets/tree/v6.11.1

Models are downloaded separately when the user clicks Download model:

- **OPUS-MT English → Russian**, Helsinki-NLP, Apache-2.0. Original:
  https://huggingface.co/Helsinki-NLP/opus-mt-en-ru
  The shipped catalogue downloads the INT8 CTranslate2 conversion from
  https://huggingface.co/jiangzhuo9357/opus-mt-en-ru-ct2 at immutable revision
  `66f1027eb3d27d8f85c9f0496d7e0b7829c07504`. This is a third-party conversion,
  not a Salvador Stream Labs-trained model or a LocalVocal download. Each file is size/hash checked.
- **Additional OPUS-MT English → target packs**, Helsinki-NLP. The small INT8
  conversions are third-party model data, downloaded directly by fixed revision
  and verified with the sizes/SHA-256 in `data/models-en-*.lock.json`. No model-hub
  scripts, Python modules or archive installers are downloaded or executed.

  | Target | Upstream model/licence | INT8 conversion |
  | --- | --- | --- |
  | German | [Helsinki-NLP/opus-mt-en-de](https://huggingface.co/Helsinki-NLP/opus-mt-en-de), CC BY 4.0 | [cstr/opus-mt-en-de-ct2-int8](https://huggingface.co/cstr/opus-mt-en-de-ct2-int8/tree/7980339e0bd2e2228748e9bfce28ba20d2e684e2) |
  | French | [Helsinki-NLP/opus-mt-en-fr](https://huggingface.co/Helsinki-NLP/opus-mt-en-fr), Apache-2.0 | [craftwise/ct2-opus-mt-en-fr-int8](https://huggingface.co/craftwise/ct2-opus-mt-en-fr-int8/tree/1bd14ddf7b73d0d5ef2dbe4982e2cfdadec9c0f2) |
  | Spanish | [Helsinki-NLP/opus-mt-en-es](https://huggingface.co/Helsinki-NLP/opus-mt-en-es), Apache-2.0 | [manancode/opus-mt-en-es-ctranslate2-android](https://huggingface.co/manancode/opus-mt-en-es-ctranslate2-android/tree/48e9476d8298d0b29f2f1d315a3e650399ae5abc) |
  | Ukrainian | [Helsinki-NLP/opus-mt-en-uk](https://huggingface.co/Helsinki-NLP/opus-mt-en-uk), Apache-2.0 | [manancode/opus-mt-en-uk-ctranslate2-android](https://huggingface.co/manancode/opus-mt-en-uk-ctranslate2-android/tree/5a09b42cd82473da42b8001467c975445961a25c) |

  Attribution: the Language Technology Research Group at the University of
  Helsinki, OPUS-MT, and the conversion publishers listed above. INT8 quantization
  changes the upstream weight representation; these are not our trained models.
  [CC BY 4.0 terms](https://creativecommons.org/licenses/by/4.0/) and
  [Apache-2.0 terms](https://www.apache.org/licenses/LICENSE-2.0).

- **lid.176.ftz**, fastText language identification, trained on Wikipedia,
  Tatoeba and SETimes. Model licensed CC BY-SA 3.0:
  https://fasttext.cc/docs/en/language-identification.html
  https://creativecommons.org/licenses/by-sa/3.0/
  The original model is downloaded unmodified with a pinned SHA-256.
- **M2M100 418M**, Facebook AI, MIT. Heavy multilingual text translation uses
  https://huggingface.co/facebook/m2m100_418M via the third-party INT8 CTranslate2
  conversion https://huggingface.co/jncraton/m2m100_418M-ct2-int8 at revision
  `7c1b2620a4e58dacecbd8bf89cfd6da7eb9eb7b0`. The five-file Heavy catalogue includes
  the unchanged fastText detector above. Weight, tokenizer, vocabulary and config
  files are all pinned by size and SHA-256 in `data/models-heavy.lock.json`.
  This is neither a Salvador Stream Labs-trained model nor a LocalVocal download. Upstream model
  documentation lists 100 languages; this plugin also requires confident fastText
  detection and leaves already-target-language, uncertain or unsupported messages unchanged.
  Its MIT notice is included as `licenses/M2M100-MIT.txt`, from the upstream
  [fairseq licence](https://github.com/facebookresearch/fairseq/blob/main/LICENSE).

OBS Studio and Twitch are independent projects/services. No affiliation or
endorsement is implied. OBS supplies its own libobs, frontend API and base Qt
runtime; Interpretwitch does not replace them. macOS supplies the HTTPS/system libraries.
