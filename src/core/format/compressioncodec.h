// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QByteArray>
#include <QString>

namespace KeyDis {

enum class CompressionFormat {
    Auto,
    Gzip,
    Zlib,
    Zstd,
    Brotli,
};

class CompressionCodec
{
public:
    static constexpr int kMaxDecodedBytes = 64 * 1024 * 1024;

    [[nodiscard]] static CompressionFormat detect(const QByteArray &data);
    [[nodiscard]] static QString name(CompressionFormat format);
    [[nodiscard]] static bool decompress(const QByteArray &data, CompressionFormat format,
                                         QByteArray *output, QString *error = nullptr,
                                         int maxBytes = kMaxDecodedBytes);
};

} // namespace KeyDis
