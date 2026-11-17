/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAICANVASMODEL_H
#define WHALEAICANVASMODEL_H

#include <QAbstractListModel>
#include <QJsonArray>
#include <QUrl>

#include <memory>
#include <optional>
#include <vector>

class KDirWatch;

/**
 * One thing the agent shows on the canvas (CV1): typed, with no code of the agent's own.
 */
struct WhaleAiCanvasBlock {
    enum class Type {
        Image,
        Video,
        Audio,
        Text,
        Files,
        Compare,
        Watch,
        Stream,
    };
    Type type = Type::Text;
    QString title;
    /** Absolute local paths: the file (image, video, audio), the files, the two of a comparison, the watched folder. */
    QStringList paths;
    /** Markdown (text) or a caption under the content (all others). */
    QString text;
    /** The stream's URL (http, https, rtsp, rtmp, srt, udp). */
    QUrl url;

    static QString typeName(Type type);
    QJsonObject toJson() const;
};

/**
 * @brief What the agent shows on the canvas of a conversation (CV1).
 *
 * The blocks come from the agent (canvas_show) and are checked before anything is shown: each
 * file must exist, a stream must be a network URL, a text has a size limit. A watched folder
 * shows its newest image or video and follows the folder. See .privatedoc/ai-canvas-plan.md.
 */
class WhaleAiCanvasModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        TypeRole = Qt::UserRole + 1,
        TitleRole,
        TextRole,
        /** The file URLs of the block (paths as file:// URLs), or the stream's URL. */
        UrlsRole,
        /** The file names of UrlsRole. */
        NamesRole,
        /** The icon names of UrlsRole (MIME type). */
        IconsRole,
        /** A watched folder: its newest image or video, else empty. */
        CurrentRole,
        /** Counts up when the current file of a watched folder changes, also when it is overwritten. */
        RevisionRole,
    };

    /** The most blocks a canvas keeps; older ones go. */
    static constexpr int MaxBlocks = 50;
    /** The most files in a block. */
    static constexpr int MaxFiles = 200;
    /** The longest text, in characters. */
    static constexpr int MaxText = 65536;

    explicit WhaleAiCanvasModel(QObject *parent = nullptr);
    ~WhaleAiCanvasModel() override;

    /** @return The block that @p json describes, or nothing with @p error set (English, for the agent). */
    static std::optional<WhaleAiCanvasBlock> parse(const QJsonObject &json, QString *error);

    /** Adds @p blocks at the end (after clearing, if @p replace); the oldest go beyond MaxBlocks. */
    void show(const QList<WhaleAiCanvasBlock> &blocks, bool replace);
    void clear();
    const WhaleAiCanvasBlock &block(int row) const;
    /** The blocks for the agent, as canvas_show takes them. */
    QJsonArray toJson() const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /** The newest image or video in @p folder, or empty. */
    static QString newestMedia(const QString &folder);

Q_SIGNALS:
    void countChanged();

private:
    struct Entry {
        WhaleAiCanvasBlock block;
        QString current;
        int revision = 0;
        std::unique_ptr<KDirWatch> watch;
    };
    void startWatching(Entry &entry);
    void updateWatched(const QString &folder);

    std::vector<std::unique_ptr<Entry>> m_entries;
};

#endif
