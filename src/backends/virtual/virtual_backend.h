/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "platform.h"

#include <kwin_export.h>

#include <QObject>
#include <QRect>

class QTemporaryDir;

namespace KWin
{
class VirtualBackend;
class VirtualOutput;

class KWIN_EXPORT VirtualBackend : public Platform
{
    Q_OBJECT
    Q_INTERFACES(KWin::Platform)
    Q_PLUGIN_METADATA(IID "org.kde.kwin.Platform" FILE "virtual.json")

public:
    VirtualBackend(QObject *parent = nullptr);
    ~VirtualBackend() override;

    Session *session() const override;
    bool initialize() override;

    bool saveFrames() const
    {
        return m_screenshotDir != nullptr;
    }
    QString screenshotDirPath() const;

    std::unique_ptr<QPainterBackend> createQPainterBackend() override;
    std::unique_ptr<OpenGLBackend> createOpenGLBackend() override;

    Q_INVOKABLE void setVirtualOutputs(int count, QVector<QRect> geometries = QVector<QRect>(), QVector<int> scales = QVector<int>());

    Outputs outputs() const override;
    Outputs enabledOutputs() const override;

    QVector<CompositingType> supportedCompositors() const override
    {
        if (selectedCompositor() != NoCompositing) {
            return {selectedCompositor()};
        }
        return QVector<CompositingType>{OpenGLCompositing, QPainterCompositing};
    }

    void enableOutput(VirtualOutput *output, bool enable);

    Q_INVOKABLE void removeOutput(Output *output);
    Q_INVOKABLE QImage captureOutput(Output *output) const;

Q_SIGNALS:
    void virtualOutputsSet(bool countChanged);

private:
    QVector<VirtualOutput *> m_outputs;
    QVector<VirtualOutput *> m_outputsEnabled;
    std::unique_ptr<QTemporaryDir> m_screenshotDir;
    std::unique_ptr<Session> m_session;
};

} // namespace KWin
