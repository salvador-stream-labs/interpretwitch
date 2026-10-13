# Awl Interpretwitch dependencies

Awl Interpretwitch source is GPL-2.0-or-later. This development package dynamically
uses OBS's Qt 6.11.1 libraries and includes Qt WebSockets 6.11.1 and the matching
Qt SecureTransport TLS backend from the checksum-pinned OBS Qt SDK. Applicable Qt
license texts, including LGPL-3.0, are included in `licenses/`. Corresponding Qt
WebSockets source is in `source.zip`; its official source and the matching OBS Qt
SDK are pinned in `dependencies.lock.json`. The shared library can be rebuilt and
replaced. Re-sign a modified bundle locally before loading it into OBS.

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
- **Qt SecureTransport backend 6.11.1**, The Qt Company, LGPL-3.0 with applicable
  exceptions. Uses macOS Security.framework for TLS and certificate verification.
  Unmodified corresponding source:
  https://github.com/qt/qtbase/tree/v6.11.1/src/plugins/tls/securetransport
  Full Qt Base source/build files: https://github.com/qt/qtbase/tree/v6.11.1

Models are downloaded separately when the user clicks Download model:

- **OPUS-MT English → Russian**, Helsinki-NLP, Apache-2.0. Original:
  https://huggingface.co/Helsinki-NLP/opus-mt-en-ru
  The shipped catalogue downloads the INT8 CTranslate2 conversion from
  https://huggingface.co/jiangzhuo9357/opus-mt-en-ru-ct2 at immutable revision
  `66f1027eb3d27d8f85c9f0496d7e0b7829c07504`. This is a third-party conversion,
  not an AWL-trained model or a LocalVocal download. Each file is size/hash checked.
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
  This is neither an AWL-trained model nor a LocalVocal download. Upstream model
  documentation lists 100 languages; this plugin also requires confident fastText
  detection and leaves Russian, uncertain or unsupported messages unchanged.
  Its MIT notice is included as `licenses/M2M100-MIT.txt`, from the upstream
  [fairseq licence](https://github.com/facebookresearch/fairseq/blob/main/LICENSE).

OBS Studio and Twitch are independent projects/services. No affiliation or
endorsement is implied. OBS supplies its own libobs, frontend API and base Qt
runtime; AWL does not replace them. macOS supplies the HTTPS/system libraries.
