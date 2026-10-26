#include "ui/FluentWidgets.hpp"

#include "ui/FluentTheme.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPropertyAnimation>
#include <QVBoxLayout>

namespace ui
{

ToggleSwitch::ToggleSwitch(QWidget* parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    animation_ = new QPropertyAnimation(this, "position", this);
    animation_->setDuration(130);
    animation_->setEasingCurve(QEasingCurve::OutCubic);
}

void ToggleSwitch::setPosition(qreal value)
{
    position_ = value;
    update();
}

void ToggleSwitch::Animate()
{
    animation_->stop();
    animation_->setStartValue(position_);
    animation_->setEndValue(isChecked() ? 1.0 : 0.0);
    animation_->start();
}

void ToggleSwitch::checkStateSet()
{
    if (!isVisible())
    {
        // Programmatic setChecked() while the page is not on screen (e.g. loading
        // settings): jump straight to the final position, no animation.
        animation_->stop();
        position_ = isChecked() ? 1.0 : 0.0;
        update();
        return;
    }
    Animate();
}

void ToggleSwitch::nextCheckState()
{
    setChecked(!isChecked());
}

void ToggleSwitch::paintEvent(QPaintEvent*)
{
    const FluentColors& c = FluentTheme::Colors();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF track(2.0, (height() - 20.0) / 2.0, 40.0, 20.0);
    const double radius = track.height() / 2.0;

    QColor fill = isChecked() ? c.accent : Qt::transparent;
    QColor stroke = isChecked() ? c.accent : c.textSecondary;
    if (!isEnabled())
    {
        fill = isChecked() ? c.textDisabled : Qt::transparent;
        stroke = c.textDisabled;
    }
    else if (underMouse() && isChecked())
    {
        fill = c.accentHover;
    }

    p.setPen(QPen(stroke, 1.0));
    p.setBrush(fill);
    p.drawRoundedRect(track.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

    // Knob: small & grey-ish when off, larger & white when on (Fluent behaviour).
    const double knob = 12.0 + 2.0 * position_;
    const double travelStart = track.left() + 4.0 + knob / 2.0;
    const double travelEnd = track.right() - 4.0 - knob / 2.0;
    const double cx = travelStart + (travelEnd - travelStart) * position_;
    QColor knobColor = isChecked() ? QColor(Qt::white) : c.textSecondary;
    if (!isEnabled())
    {
        knobColor = c.textDisabled;
    }
    p.setPen(Qt::NoPen);
    p.setBrush(knobColor);
    p.drawEllipse(QPointF(cx, track.center().y()), knob / 2.0, knob / 2.0);

    if (hasFocus())
    {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c.accentText, 1.5));
        p.drawRoundedRect(track.adjusted(-2, -2, 2, 2), radius + 2, radius + 2);
    }
}

QFrame* MakeSettingRow(const QString& title, const QString& description, QWidget* control, QWidget* parent)
{
    auto* row = new QFrame(parent);
    row->setObjectName(QStringLiteral("settingRow"));
    row->setFrameShape(QFrame::NoFrame);

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(16);

    auto* textColumn = new QVBoxLayout();
    textColumn->setSpacing(2);
    auto* titleLabel = new QLabel(title, row);
    titleLabel->setObjectName(QStringLiteral("rowTitle"));
    titleLabel->setWordWrap(true);
    textColumn->addWidget(titleLabel);
    if (!description.isEmpty())
    {
        auto* descLabel = new QLabel(description, row);
        descLabel->setObjectName(QStringLiteral("rowDesc"));
        descLabel->setWordWrap(true);
        textColumn->addWidget(descLabel);
    }
    layout->addLayout(textColumn, 1);

    control->setParent(row);
    layout->addWidget(control, 0, Qt::AlignVCenter);
    return row;
}

QFrame* MakeToggleRow(const QString& title, const QString& description, ToggleSwitch*& toggleOut, QWidget* parent)
{
    toggleOut = new ToggleSwitch();
    QFrame* row = MakeSettingRow(title, description, toggleOut, parent);
    return row;
}

void AddPageHeader(QVBoxLayout* layout, const QString& title, const QString& subtitle)
{
    auto* titleLabel = new QLabel(title);
    titleLabel->setObjectName(QStringLiteral("titleLabel"));
    layout->addWidget(titleLabel);
    if (!subtitle.isEmpty())
    {
        auto* sub = new QLabel(subtitle);
        sub->setObjectName(QStringLiteral("subtitleLabel"));
        sub->setWordWrap(true);
        layout->addWidget(sub);
    }
}

QLabel* MakeSectionLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("sectionLabel"));
    return label;
}

} // namespace ui
