#pragma once

#include "host/clipboard/IClipboardBackend.hpp"

#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QIODevice>
#include <QList>
#include <QMimeData>
#include <QUrl>

namespace havel::host {

class QtClipboardBackend : public IClipboardBackend {
public:
    QtClipboardBackend() = default;
    ~QtClipboardBackend() override = default;

    std::string getText() const override {
        auto *cb = clipboard();
        if (!cb) return "";
        return cb->text().toStdString();
    }

    bool setText(const std::string &text) override {
        auto *cb = clipboard();
        if (!cb) return false;
        cb->setText(QString::fromStdString(text));
        return true;
    }

    bool clear() override {
        return setText("");
    }

    bool hasText() const override {
        return !getText().empty();
    }

    std::string getImage() const override {
        auto *cb = clipboard();
        if (!cb) return "";
        QImage image = cb->image();
        if (image.isNull()) return "";
        QByteArray byteArray;
        QBuffer buffer(&byteArray);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return QByteArray::fromRawData(byteArray.constData(), byteArray.size())
            .toBase64()
            .toStdString();
    }

    bool setImage(const std::string &base64Png) override {
        auto *cb = clipboard();
        if (!cb) return false;
        QByteArray byteArray = QByteArray::fromBase64(QByteArray::fromStdString(base64Png));
        QImage image;
        image.loadFromData(byteArray, "PNG");
        if (image.isNull()) return false;
        cb->setImage(image);
        return true;
    }

    bool hasImage() const override {
        return !getImage().empty();
    }

    std::vector<std::string> getFiles() const override {
        auto *cb = clipboard();
        if (!cb) return {};
        const QMimeData *mimeData = cb->mimeData();
        if (!mimeData || !mimeData->hasUrls()) return {};
        std::vector<std::string> files;
        for (const QUrl &url : mimeData->urls()) {
            if (url.isLocalFile()) {
                files.push_back(url.toLocalFile().toStdString());
            }
        }
        return files;
    }

    bool setFiles(const std::vector<std::string> &paths) override {
        auto *cb = clipboard();
        if (!cb) return false;
        QList<QUrl> urls;
        for (const auto &path : paths) {
            urls.append(QUrl::fromLocalFile(QString::fromStdString(path)));
        }
        QMimeData *mimeData = new QMimeData();
        mimeData->setUrls(urls);
        cb->setMimeData(mimeData);
        return true;
    }

    bool hasFiles() const override {
        return !getFiles().empty();
    }

private:
    static QClipboard *clipboard() {
        if (!QGuiApplication::instance()) return nullptr;
        return QGuiApplication::clipboard();
    }
};

} // namespace havel::host
