#pragma once

#include <QFont>
#include <QImage>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QSet>
#include <QTextLayout>
#include <QtMath>

// Setup screens are rendered once at creation. A generated show uses ordinary
// image sources, with no embedded browser, network request or running timer.
namespace PulseSetupCards {
inline QString problem(const QJsonObject &spec)
{
	const QSet<QString> allowed{"kind", "name", "text", "subtitle", "style", "backgroundOnly"};
	for (auto field = spec.begin(); field != spec.end(); ++field)
		if (!allowed.contains(field.key())) return "The setup card contains an unsupported option.";
	if (spec.value("kind").toString() != "setup_card") return "Choose a static setup card.";
	if (!spec.value("name").isString() || spec.value("name").toString().trimmed().isEmpty() ||
	    spec.value("name").toString().size() > 120) return "Give the setup card a name of 1–120 characters.";
	for (const auto character : spec.value("name").toString())
		if (character.isNull() || character.isNonCharacter() || character.category() == QChar::Other_Control)
			return "Remove control characters from the setup card name.";
	if (spec.contains("backgroundOnly") && !spec.value("backgroundOnly").isBool())
		return "Choose whether this setup card is a background or a message.";
	const bool backgroundOnly = spec.value("backgroundOnly").toBool();
	for (const QString &key : {QString("text"), QString("subtitle")}) {
		if (spec.contains(key) && !spec.value(key).isString()) return "Use plain text for the setup card.";
		const QString text = spec.value(key).toString();
		if (text.size() > (key == "text" ? 160 : 320)) return "Shorten the setup card's title or subtitle.";
		for (const auto character : text)
			if (character.isNull() || character.isNonCharacter() ||
			    (character.category() == QChar::Other_Control && character != QChar('\n')))
				return "Remove control characters from the setup card text.";
	}
	if (!backgroundOnly && spec.value("text").toString().trimmed().isEmpty())
		return "Enter a title for the setup screen.";
	if (spec.contains("style") && !spec.value("style").isString()) return "Choose a setup card colour style.";
	const QSet<QString> styles{"navy", "cyan", "violet", "amber"};
	if (!styles.contains(spec.value("style").toString("navy"))) return "Choose an available setup card colour style.";
	return {};
}

inline qreal textHeight(const QString &text, const QFont &font, qreal width, QPainter *painter = nullptr,
			const QPointF &origin = {})
{
	qreal height = 0;
	for (const auto &paragraph : text.split('\n')) {
		QTextLayout layout(paragraph.isEmpty() ? QString(" ") : paragraph, font);
		QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
		layout.setTextOption(option); layout.beginLayout();
		while (true) {
			auto line = layout.createLine(); if (!line.isValid()) break;
			line.setLineWidth(qMax(qreal(1), width)); line.setPosition({0, height}); height += line.height();
		}
		layout.endLayout();
		if (painter) layout.draw(painter, origin);
	}
	return height;
}

inline QFont fittedFont(const QString &text, const QRectF &bounds, int preferredSize, bool title)
{
	QFont font("Segoe UI"); font.setWeight(title ? QFont::DemiBold : QFont::Normal);
	int size = qMax(1, preferredSize);
	for (; size > 1; --size) {
		font.setPixelSize(size);
		if (textHeight(text, font, bounds.width()) <= bounds.height()) return font;
	}
	font.setPixelSize(1); return font;
}

inline bool dimensionsSupported(const QSize &size)
{
	return size.width() > 0 && size.height() > 0 && size.width() <= 16384 && size.height() <= 16384 &&
		qint64(size.width()) * size.height() <= 33554432;
}

inline QImage render(const QJsonObject &spec, const QSize &size)
{
	if (!problem(spec).isEmpty() || !dimensionsSupported(size)) return {};
	QImage image(size, QImage::Format_RGB32); if (image.isNull()) return {};
	const qreal width = size.width(), height = size.height(), unit = qMin(width, height);
	const QString style = spec.value("style").toString("navy");
	const QColor accent(style == "violet" ? "#BEA1F3" : style == "amber" ? "#FFCD76" :
		style == "cyan" ? "#67D9EA" : "#91B8EF");
	QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing);
	const QColor top(style == "violet" ? "#17152B" : style == "amber" ? "#211B14" :
		style == "cyan" ? "#091E29" : "#0A1525");
	const QColor bottom(style == "violet" ? "#322B52" : style == "amber" ? "#3F3023" :
		style == "cyan" ? "#153A49" : "#203A56");
	QLinearGradient base(0, 0, width, height); base.setColorAt(0, top); base.setColorAt(1, bottom);
	painter.fillRect(image.rect(), base);
	QColor glow = accent; glow.setAlpha(24);
	QRadialGradient light(QPointF(width * .85, height * .2), qMax(width, height) * .65);
	light.setColorAt(0, glow); light.setColorAt(1, QColor(0, 0, 0, 0)); painter.fillRect(image.rect(), light);
	QPainterPath band; band.moveTo(width * .70, 0); band.lineTo(width, 0); band.lineTo(width, height * .87);
	band.lineTo(width * .89, height); band.lineTo(width * .23, height); band.closeSubpath();
	painter.fillPath(band, QColor(255, 255, 255, 4));
	painter.setPen(QPen(QColor(112, 151, 182, 35), qMax(qreal(1), unit * .002)));
	painter.drawLine(QPointF(width * .72, 0), QPointF(width, height * .37));
	painter.drawLine(QPointF(width * .88, 0), QPointF(width, height * .16));
	if (spec.value("backgroundOnly").toBool()) return image;
	const bool portrait = height > width;
	const qreal left = width * .10, usableWidth = width * .80;
	const QRectF titleBounds(left, height * (portrait ? .30 : .28), usableWidth, height * .32);
	const QRectF subtitleBounds(left, height * .64, usableWidth, height * .17);
	const QString title = spec.value("text").toString().trimmed(), subtitle = spec.value("subtitle").toString().trimmed();
	QFont titleFont = fittedFont(title, titleBounds, qRound(qMin(width * .075, height * .14)), true);
	const qreal actualTitleHeight = textHeight(title, titleFont, titleBounds.width());
	const QPointF titleOrigin(titleBounds.left(), titleBounds.top() + (titleBounds.height() - actualTitleHeight) / 2);
	painter.setPen(QColor("#F4F8FE")); textHeight(title, titleFont, titleBounds.width(), &painter, titleOrigin);
	painter.setPen(QPen(accent, qMax(qreal(2), unit * .006), Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(QPointF(left, height * .235), QPointF(left + qMin(width * .18, unit * .24), height * .235));
	if (!subtitle.isEmpty()) {
		const QFont subtitleFont = fittedFont(subtitle, subtitleBounds, qRound(unit * .037), false);
		painter.setPen(QColor("#BDCDDE")); textHeight(subtitle, subtitleFont, subtitleBounds.width(), &painter, subtitleBounds.topLeft());
	}
	QFont label("Segoe UI"); label.setPixelSize(qMax(1, qRound(unit * .021))); label.setWeight(QFont::DemiBold);
	label.setLetterSpacing(QFont::PercentageSpacing, 120); painter.setFont(label); painter.setPen(QColor("#9DB8D1"));
	painter.drawText(QRectF(left, height * .89, usableWidth, height * .05), Qt::AlignLeft | Qt::AlignVCenter, "PULSE WEAVER");
	return image;
}

inline bool write(const QString &path, const QJsonObject &spec, const QSize &size)
{
	const QImage image = render(spec, size); if (image.isNull()) return false;
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG")) return false;
	return file.commit();
}
} // namespace PulseSetupCards
