/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALESTYLES_H
#define WHALESTYLES_H

#include "whalestylestore.h"

#include <QObject>

/**
 * @brief The styles of the process, shared by all windows (ST3).
 *
 * Every change is saved at once, so another process reads it the next time it reloads (when
 * its Styles menu opens). changed() tells the windows to rebuild their style actions.
 */
class WhaleStyles : public QObject
{
    Q_OBJECT

public:
    static WhaleStyles *self();

    explicit WhaleStyles(KSharedConfigPtr config, QObject *parent = nullptr);
    ~WhaleStyles() override;

    const WhaleStyleStore &store() const;

    /** @return The store to change; save() afterwards. */
    WhaleStyleStore &editableStore();

    /** Writes the styles and emits changed(). */
    void save();

    /** Reads the styles again (another process may have changed them); emits changed() if they differ. */
    void reload();

Q_SIGNALS:
    void changed();

private:
    WhaleStyleStore m_store;
};

#endif
