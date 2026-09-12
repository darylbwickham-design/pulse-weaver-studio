#include <QApplication>
#include <QDirIterator>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QFileInfo>
#include <QPushButton>
#include <QFrame>
#include <QFile>
#include <QStyle>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try {
        if (argc != 2) throw std::runtime_error("Pass the PulseWeaver asset directory");
        const QString root = QString::fromLocal8Bit(argv[1]);
        QPushButton sample("Go live");
        sample.resize(180, 52);
        sample.setStyleSheet("QPushButton { color: white; border-radius: 0; padding: 9px 16px; background: transparent; border: 7px solid transparent; border-image: url(\"" + QFileInfo(root + "/backstage/live.svg").absoluteFilePath() + "\") 14 14 14 14 stretch stretch; }");
        QImage sampleImage(sample.size(), QImage::Format_ARGB32);
        sampleImage.fill(Qt::transparent);
        sample.render(&sampleImage);
        if (qRed(sampleImage.pixel(90, 2)) < 120)
            throw std::runtime_error("The illuminated button edge is clipped");
        sampleImage.save("artifacts/v45-button-render.png");
        QList<QImage> silhouettes;
        int total = 0;
        for (const QString &preset : {QString("backstage"), QString("marquee"), QString("electric")}) {
            QDir::addSearchPath("theme", QFileInfo(root).absolutePath());
            const QString themeFile = preset == "backstage" ? "PulseWeaver.ovt" :
                "PulseWeaver_" + preset.left(1).toUpper() + preset.mid(1) + ".ovt";
            QFile theme(QFileInfo(root).absolutePath() + '/' + themeFile);
            if (!theme.open(QIODevice::ReadOnly)) throw std::runtime_error("Missing preset stylesheet");
            QString panelRules;
            for (const QString &line : QString::fromUtf8(theme.readAll()).split('\n'))
                if (line.startsWith("QFrame#PulseWeaverStage") || line.startsWith("QFrame#PulseWeaverStreamStats"))
                    panelRules += line + '\n';
            for (const QString &objectName : {QString("PulseWeaverStage"), QString("PulseWeaverStreamStats")}) {
                QFrame panel;
                panel.setObjectName(objectName);
                panel.setStyleSheet(panelRules);
                for (int width : {230, 700}) {
                    panel.resize(width, 120);
                    for (const auto &entry : QList<QPair<QString, QString>>{
                        {"twitch", "#9146FF"}, {"youtube", "#FF3B30"}, {"kick", "#53FC18"}, {"recording", "#FFAD38"}}) {
                        const char *property = objectName == "PulseWeaverStage" ? "pulseWeaverPreviewProvider" : "pulseWeaverStatsAccent";
                        panel.setProperty(property, objectName == "PulseWeaverStage" ? entry.first : entry.second);
                        panel.style()->unpolish(&panel);
                        panel.style()->polish(&panel);
                        QImage rendered(panel.size(), QImage::Format_ARGB32);
                        rendered.fill(Qt::transparent);
                        panel.render(&rendered);
                        // QFrame's native frame metrics can shift a sliced border by a
                        // pixel. Check its top edge, rather than assuming one scanline.
                        const QColor expected(entry.second);
                        int closest = 765;
                        for (int y = 0; y < 7; ++y) {
                            const QColor actual = rendered.pixelColor(width / 2, y);
                            closest = std::min(closest, std::abs(actual.red() - expected.red()) +
                                std::abs(actual.green() - expected.green()) + std::abs(actual.blue() - expected.blue()));
                        }
                        if (closest > 6)
                            throw std::runtime_error(("Platform stripe missing while cycling " + preset + '/' + objectName + '/' + entry.first).toStdString());
                        if (objectName == "PulseWeaverStage" && width == 230)
                            rendered.save("artifacts/v46-" + preset + '-' + entry.first + ".png");
                    }
                }
            }
            QDirIterator files(root + '/' + preset, {"*.svg"}, QDir::Files, QDirIterator::Subdirectories);
            int count = 0;
            while (files.hasNext()) {
                const QString file = files.next();
                QSvgRenderer renderer(file);
                if (!renderer.isValid()) throw std::runtime_error(("Invalid SVG: " + file).toStdString());
                for (int scale : {1, 2}) {
                    QImage image(renderer.defaultSize() * scale, QImage::Format_ARGB32);
                    image.fill(Qt::transparent);
                    QPainter painter(&image);
                    renderer.render(&painter);
                    painter.end();
                    int painted = 0;
                    for (int y = 0; y < image.height(); ++y)
                        for (int x = 0; x < image.width(); ++x)
                            if (qAlpha(image.pixel(x, y))) ++painted;
                    if (painted < 20) throw std::runtime_error(("Empty rendered asset: " + file).toStdString());
                    if (file.endsWith("/icons/show.svg") && scale == 1) {
                        for (int y = 0; y < image.height(); ++y)
                            for (int x = 0; x < image.width(); ++x)
                                image.setPixel(x, y, qAlpha(image.pixel(x, y)) > 127 ? qRgb(255,255,255) : qRgb(0,0,0));
                        silhouettes.append(image);
                    }
                }
                ++count;
            }
            if (count != 77) throw std::runtime_error("Incomplete theme asset set");
            total += count;
        }
        if (silhouettes.size() != 3 || silhouettes[0] == silhouettes[1] || silhouettes[0] == silhouettes[2] || silhouettes[1] == silhouettes[2])
            throw std::runtime_error("Themes must have different icon silhouettes, independently of colour");
        std::cout << "PASS: " << total << " native SVG assets render at 1x and 2x; three distinct icon silhouettes\n";
        std::cout << "PASS: platform colour survives repeated cycling in both previews and stats, all three themes, at narrow and wide sizes\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
