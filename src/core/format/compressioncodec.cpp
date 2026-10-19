// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/format/compressioncodec.h"

#include <brotli/decode.h>
#include <zlib.h>
#include <zstd.h>

#include <array>

namespace KeyDis {

namespace {

QString zlibError(int code)
{
    switch (code) {
    case Z_DATA_ERROR:
        return QStringLiteral("Invalid compressed data");
    case Z_MEM_ERROR:
        return QStringLiteral("Out of memory while decompressing");
    case Z_BUF_ERROR:
        return QStringLiteral("Compressed data ended unexpectedly");
    default:
        return QStringLiteral("zlib decompression failed (%1)").arg(code);
    }
}

bool inflateData(const QByteArray &data, int windowBits, QByteArray *output, QString *error,
                 int maxBytes)
{
    z_stream stream{};
    int code = inflateInit2(&stream, windowBits);
    if (code != Z_OK) {
        if (error)
            *error = zlibError(code);
        return false;
    }
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = static_cast<uInt>(data.size());
    std::array<char, 64 * 1024> buffer{};
    QByteArray result;
    do {
        stream.next_out = reinterpret_cast<Bytef *>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        code = inflate(&stream, Z_NO_FLUSH);
        if (code != Z_OK && code != Z_STREAM_END) {
            if (error)
                *error = zlibError(code);
            inflateEnd(&stream);
            return false;
        }
        const int produced = static_cast<int>(buffer.size() - stream.avail_out);
        if (result.size() + produced > maxBytes) {
            if (error)
                *error = QStringLiteral("Decoded data exceeds the %1 MiB limit")
                             .arg(maxBytes / (1024 * 1024));
            inflateEnd(&stream);
            return false;
        }
        result.append(buffer.data(), produced);
    } while (code != Z_STREAM_END && stream.avail_in > 0);
    inflateEnd(&stream);
    if (code != Z_STREAM_END) {
        if (error)
            *error = QStringLiteral("Compressed data ended unexpectedly");
        return false;
    }
    *output = result;
    return true;
}

bool decompressZstd(const QByteArray &data, QByteArray *output, QString *error, int maxBytes)
{
    ZSTD_DStream *stream = ZSTD_createDStream();
    if (!stream) {
        if (error)
            *error = QStringLiteral("Could not create zstd decoder");
        return false;
    }
    size_t code = ZSTD_initDStream(stream);
    if (ZSTD_isError(code)) {
        if (error)
            *error = QString::fromUtf8(ZSTD_getErrorName(code));
        ZSTD_freeDStream(stream);
        return false;
    }
    ZSTD_inBuffer input{data.constData(), static_cast<size_t>(data.size()), 0};
    std::array<char, 64 * 1024> buffer{};
    QByteArray result;
    while (input.pos < input.size) {
        ZSTD_outBuffer out{buffer.data(), buffer.size(), 0};
        code = ZSTD_decompressStream(stream, &out, &input);
        if (ZSTD_isError(code)) {
            if (error)
                *error = QString::fromUtf8(ZSTD_getErrorName(code));
            ZSTD_freeDStream(stream);
            return false;
        }
        if (result.size() + static_cast<int>(out.pos) > maxBytes) {
            if (error)
                *error = QStringLiteral("Decoded data exceeds the %1 MiB limit")
                             .arg(maxBytes / (1024 * 1024));
            ZSTD_freeDStream(stream);
            return false;
        }
        result.append(buffer.data(), static_cast<int>(out.pos));
        if (code == 0 && input.pos == input.size)
            break;
        if (out.pos == 0 && input.pos == input.size) {
            if (error)
                *error = QStringLiteral("Compressed data ended unexpectedly");
            ZSTD_freeDStream(stream);
            return false;
        }
    }
    ZSTD_freeDStream(stream);
    *output = result;
    return true;
}

bool decompressBrotli(const QByteArray &data, QByteArray *output, QString *error, int maxBytes)
{
    BrotliDecoderState *state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    if (!state) {
        if (error)
            *error = QStringLiteral("Could not create Brotli decoder");
        return false;
    }
    const uint8_t *nextIn = reinterpret_cast<const uint8_t *>(data.constData());
    size_t availableIn = static_cast<size_t>(data.size());
    std::array<uint8_t, 64 * 1024> buffer{};
    QByteArray result;
    BrotliDecoderResult code = BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT;
    while (code == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT
           || code == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) {
        uint8_t *nextOut = buffer.data();
        size_t availableOut = buffer.size();
        code = BrotliDecoderDecompressStream(state, &availableIn, &nextIn, &availableOut,
                                             &nextOut, nullptr);
        const int produced = static_cast<int>(buffer.size() - availableOut);
        if (result.size() + produced > maxBytes) {
            if (error)
                *error = QStringLiteral("Decoded data exceeds the %1 MiB limit")
                             .arg(maxBytes / (1024 * 1024));
            BrotliDecoderDestroyInstance(state);
            return false;
        }
        result.append(reinterpret_cast<const char *>(buffer.data()), produced);
        if (code == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT && availableIn == 0)
            break;
    }
    BrotliDecoderDestroyInstance(state);
    if (code != BROTLI_DECODER_RESULT_SUCCESS) {
        if (error)
            *error = QStringLiteral("Invalid Brotli data");
        return false;
    }
    *output = result;
    return true;
}

} // namespace

CompressionFormat CompressionCodec::detect(const QByteArray &data)
{
    if (data.size() >= 2 && static_cast<unsigned char>(data.at(0)) == 0x1f
        && static_cast<unsigned char>(data.at(1)) == 0x8b) {
        return CompressionFormat::Gzip;
    }
    if (data.size() >= 4 && static_cast<unsigned char>(data.at(0)) == 0x28
        && static_cast<unsigned char>(data.at(1)) == 0xb5
        && static_cast<unsigned char>(data.at(2)) == 0x2f
        && static_cast<unsigned char>(data.at(3)) == 0xfd) {
        return CompressionFormat::Zstd;
    }
    if (data.size() >= 2 && (static_cast<unsigned char>(data.at(0)) & 0x0f) == 0x08
        && ((static_cast<unsigned char>(data.at(0)) << 8)
            | static_cast<unsigned char>(data.at(1))) % 31 == 0) {
        return CompressionFormat::Zlib;
    }
    return CompressionFormat::Auto;
}

QString CompressionCodec::name(CompressionFormat format)
{
    switch (format) {
    case CompressionFormat::Gzip:
        return QStringLiteral("gzip");
    case CompressionFormat::Zlib:
        return QStringLiteral("zlib");
    case CompressionFormat::Zstd:
        return QStringLiteral("zstd");
    case CompressionFormat::Brotli:
        return QStringLiteral("brotli");
    case CompressionFormat::Auto:
        return QStringLiteral("auto");
    }
    return QStringLiteral("unknown");
}

bool CompressionCodec::decompress(const QByteArray &data, CompressionFormat format,
                                  QByteArray *output, QString *error, int maxBytes)
{
    if (!output) {
        if (error)
            *error = QStringLiteral("No output buffer");
        return false;
    }
    if (data.isEmpty()) {
        if (error)
            *error = QStringLiteral("Empty input");
        return false;
    }
    if (maxBytes <= 0)
        maxBytes = kMaxDecodedBytes;
    switch (format) {
    case CompressionFormat::Gzip:
        return inflateData(data, 15 + 16, output, error, maxBytes);
    case CompressionFormat::Zlib:
        return inflateData(data, 15, output, error, maxBytes);
    case CompressionFormat::Zstd:
        return decompressZstd(data, output, error, maxBytes);
    case CompressionFormat::Brotli:
        return decompressBrotli(data, output, error, maxBytes);
    case CompressionFormat::Auto: {
        const CompressionFormat detected = detect(data);
        if (detected == CompressionFormat::Auto) {
            if (error)
                *error = QStringLiteral("Unknown compression format");
            return false;
        }
        return decompress(data, detected, output, error, maxBytes);
    }
    }
    return false;
}

} // namespace KeyDis
