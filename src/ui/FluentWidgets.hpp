#pragma once

// Small reusable Fluent-style building blocks for the settings pages.

#include <QAbstractButton>
#include <QFrame>
#include <QString>

class QLabel;
class QPropertyAnimation;
class QWidget;
class QVBoxLayout;

namespace ui
{

// Fluent "ToggleSwitch": a 40x20 pill with a sliding knob. Behaves like a
// checkable button (isChecked()/setChecked()/toggled()).
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(qreal position READ position WRITE setPosition)

public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override { return {44, 24}; }
    [[nodiscard]] qreal position() const { return position_; }
    void setPosition(qreal value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void checkStateSet() override;
    void nextCheckState() override;

private:
    void Animate();

    qreal position_ = 0.0; // 0 = off (left) .. 1 = on (right)
    QPropertyAnimation* animation_ = nullptr;
};

// A Fluent "settings card" row: title + description on the left, the
// control on the right. `control` is reparented into the row.
QFrame* MakeSettingRow(const QString& title, const QString& description, QWidget* control, QWidget* parent = nullptr);

// Convenience: row whose control is a new ToggleSwitch, returned via out param.
QFrame* MakeToggleRow(const QString& title, const QString& description, ToggleSwitch*& toggleOut, QWidget* parent = nullptr);

// Page header (large title + optional subtitle) added to `layout`.
void AddPageHeader(QVBoxLayout* layout, const QString& title, const QString& subtitle = QString());

// Small all-caps-ish section heading.
QLabel* MakeSectionLabel(const QString& text, QWidget* parent = nullptr);

} // namespace ui
