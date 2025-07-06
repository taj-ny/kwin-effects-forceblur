/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "blur_config.h"

//#include <config-kwin.h>

// KConfigSkeleton
#include "blurconfig.h"

#include <KPluginFactory>
#include "kwineffects_interface.h"

#include <QFileDialog>
#include <QPushButton>

namespace KWin
{

K_PLUGIN_CLASS(BlurEffectConfig)

BlurEffectConfig::BlurEffectConfig(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
{
    ui.setupUi(widget());
    BlurConfig::instance("kwinrc");
    addConfig(BlurConfig::self(), widget());

    connect(ui.staticBlurImagePicker, &QPushButton::clicked, this, &BlurEffectConfig::slotStaticBlurImagePickerClicked);

    QFile about(":/effects/forceblur/kcm/about.html");
    if (about.open(QIODevice::ReadOnly)) {
        const auto html = about.readAll()
            .replace("${version}", ABOUT_VERSION_STRING)
            .replace("${repo}", "https://github.com/taj-ny/kwin-effects-forceblur");
        ui.aboutText->setHtml(html);
    }
}

BlurEffectConfig::~BlurEffectConfig()
{
}

void BlurEffectConfig::slotStaticBlurImagePickerClicked()
{
    const auto imagePath = QFileDialog::getOpenFileName(widget(), "Select image", {}, "Images (*.png *.jpg *.jpeg *.bmp)");
    if (imagePath.isNull()) {
        return;
    }

    ui.kcfg_FakeBlurImage->setText(imagePath);
}

void BlurEffectConfig::save()
{
    KCModule::save();

    OrgKdeKwinEffectsInterface interface(QStringLiteral("org.kde.KWin"),
                                         QStringLiteral("/Effects"),
                                         QDBusConnection::sessionBus());

    if (QGuiApplication::platformName() == QStringLiteral("xcb")) {
        interface.reconfigureEffect(QStringLiteral("forceblur_x11"));
    } else {
        interface.reconfigureEffect(QStringLiteral("forceblur"));
    }
}

} // namespace KWin

#include "blur_config.moc"

#include "moc_blur_config.cpp"
