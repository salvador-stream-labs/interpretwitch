// SPDX-License-Identifier: GPL-2.0-or-later
#include "model.hpp"
#include "limits.hpp"
#include "short_replies.hpp"
#include <QDir>
#include <QHash>
#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>
#include <fasttext.h>
#include <sstream>
#include <stdexcept>

namespace awl::chat {
struct Translator::Impl {
    sentencepiece::SentencePieceProcessor source, target;
    fasttext::FastText language;
    std::unique_ptr<ctranslate2::Translator> translator;
    ModelProfile profile = ModelProfile::Light;
    QString target_language;
    std::string target_token;
    QHash<QString, Translation> cache;
    std::deque<QString> order;
};
Translator::Translator(const QString &directory, RunOn run_on, ModelProfile profile, QString target)
    : impl_(std::make_unique<Impl>()) {
    if (!supports_target(profile, target))
        throw std::runtime_error("Unsupported target language.");
    impl_->profile = profile;
    impl_->target_language = target;
    impl_->target_token = "__" + target.toStdString() + "__";
    const bool heavy = profile == ModelProfile::Heavy;
    auto device = ctranslate2::Device::CPU;
    auto compute = ctranslate2::ComputeType::INT8;
    if (run_on == RunOn::GPU) {
        // Resolve through the library so CPU-only builds fail explicitly. CUDA
        // initialization stays on the helper process, outside the OBS UI thread.
        device = ctranslate2::str_to_device("cuda");
        if (ctranslate2::get_device_count(device) < 1)
            throw std::runtime_error("No compatible NVIDIA GPU is available.");
        compute = ctranslate2::resolve_compute_type(ctranslate2::ComputeType::INT8_FLOAT16,
                                                    ctranslate2::ComputeType::INT8_FLOAT32, device,
                                                    0, true);
    }
    QDir dir(directory);
    if (!impl_->source
             .Load(dir.filePath(heavy ? "sentencepiece.bpe.model" : "source.spm").toStdString())
             .ok() ||
        (!heavy && !impl_->target.Load(dir.filePath("target.spm").toStdString()).ok()))
        throw std::runtime_error("Cannot load translation vocabulary.");
    impl_->language.loadModel(dir.filePath("lid.176.ftz").toStdString());
    ctranslate2::ReplicaPoolConfig config;
    // Sparse incoming chat can wait: Heavy deliberately uses one CPU thread.
    config.num_threads_per_replica = heavy ? 1 : 2;
    config.max_queued_batches = 1;
    impl_->translator = std::make_unique<ctranslate2::Translator>(
        directory.toStdString(), device, compute, std::vector<int>{0}, false, config);
    if (run_on == RunOn::GPU) {
        // CUDA libraries can be loaded lazily on the first decode. Exercise a
        // tiny fixed input before declaring GPU ready, so those startup errors
        // also reach the worker's CPU fallback. No chat content is involved.
        std::vector<std::string> tokens;
        impl_->source.Encode("Hello", &tokens);
        tokens.push_back("</s>");
        if (heavy)
            tokens.insert(tokens.begin(), "__en__");
        ctranslate2::TranslationOptions warmup;
        warmup.beam_size = 1;
        warmup.max_decoding_length = 2;
        impl_->translator->translate_batch(
            {tokens},
            {heavy ? std::vector<std::string>{impl_->target_token} : std::vector<std::string>{}},
            warmup);
        ctranslate2::synchronize_device(device, 0);
    }
}
Translator::~Translator() = default;
void Translator::clear_cache() {
    impl_->cache.clear();
    impl_->order.clear();
}
Translation Translator::translate(const Message &message, const std::function<bool()> &cancel) {
    if (cancel())
        return {message.original};
    QString key;
    for (const auto &part : message.parts)
        key +=
            (part.protected_text ? "P" : "T") + QString::number(part.text.size()) + ":" + part.text;
    if (impl_->cache.contains(key))
        return impl_->cache.value(key);
    QString detection;
    size_t text_parts = 0;
    for (const auto &part : message.parts)
        if (!part.protected_text) {
            detection += part.text + ' ';
            if (!part.text.trimmed().isEmpty())
                ++text_parts;
        }
    detection = detection.simplified();
    Translation result{message.original};
    if (detection.isEmpty())
        return result;
    std::istringstream input(detection.toStdString());
    std::vector<std::pair<fasttext::real, std::string>> language;
    impl_->language.predictLine(input, language, 2, 0.0);
    const auto detected = language.empty()
                              ? QString{}
                              : QString::fromStdString(language[0].second).mid(9); // __label__
    const auto source_language = translation_language(
        detection, detected, language.empty() ? 0.f : language[0].first, impl_->profile,
        language.size() > 1 ? language[1].first : 0.f, impl_->target_language);
    if (source_language.isEmpty())
        return result;
    const bool heavy = impl_->profile == ModelProfile::Heavy;
    QString translated;
    size_t pieces = 0;
    for (const auto &part : message.parts) {
        if (cancel())
            return {message.original};
        if (part.protected_text || part.text.trimmed().isEmpty()) {
            translated += part.text;
            continue;
        }
        if (++pieces > limits::translated_portions)
            return {message.original, false};
        // Apply fixed replies only to the entire text, never to isolated pieces
        // of a sentence split by a mention, link or emote. Keep protected parts.
        if (source_language == "en" && text_parts == 1) {
            const auto reply = short_reply(part.text, impl_->target_language);
            if (!reply.isEmpty()) {
                translated += reply;
                continue;
            }
        }
        std::vector<std::string> tokens;
        impl_->source.Encode(part.text.trimmed().toStdString(), &tokens);
        if (tokens.size() > limits::input_tokens)
            return {message.original, false};
        tokens.push_back("</s>");
        if (heavy)
            tokens.insert(tokens.begin(), "__" + source_language.toStdString() + "__");
        ctranslate2::TranslationOptions options;
        options.beam_size = 1;
        options.max_decoding_length = limits::output_tokens;
        options.no_repeat_ngram_size = 4;
        options.callback = [&](auto) {
            return cancel();
        };
        const auto output = impl_->translator->translate_batch(
            {tokens},
            {heavy ? std::vector<std::string>{impl_->target_token} : std::vector<std::string>{}},
            options);
        if (cancel())
            return {message.original};
        if (output.empty() || output[0].hypotheses.empty() ||
            output[0].hypotheses[0].size() >= limits::output_tokens)
            return {message.original, false};
        std::string decoded;
        auto hypothesis = output[0].hypotheses[0];
        if (heavy) {
            if (hypothesis.empty() || hypothesis.front() != impl_->target_token)
                return {message.original, false};
            hypothesis.erase(hypothesis.begin());
            impl_->source.Decode(hypothesis, &decoded);
        } else
            impl_->target.Decode(hypothesis, &decoded);
        const auto text = QString::fromStdString(decoded).trimmed();
        if (text.isEmpty() || repetitive(text))
            return {message.original, false};
        if (!part.text.isEmpty() && part.text.front().isSpace())
            translated += ' ';
        translated += text;
        if (!part.text.isEmpty() && part.text.back().isSpace())
            translated += ' ';
    }
    result = {translated.trimmed(), true, source_language};
    if (impl_->order.size() >= limits::cached_translations) {
        impl_->cache.remove(impl_->order.front());
        impl_->order.pop_front();
    }
    impl_->order.push_back(key);
    impl_->cache.insert(key, result);
    return result;
}
} // namespace awl::chat
